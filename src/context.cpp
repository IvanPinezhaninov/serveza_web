/******************************************************************************
**
** Copyright (C) 2026 Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>
**
** This file is part of serveza_web, which can be found at
** https://github.com/IvanPinezhaninov/serveza_web/.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
** DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
** OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
** OR OTHER DEALINGS IN THE SOFTWARE.
**
******************************************************************************/

#include <serveza/web/context.h>

#include <array>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/system/errc.hpp>

#include <serveza/web/errors.h>

#include "detail/core.h"

namespace serveza::web {

namespace http = boost::beast::http;
namespace net = boost::asio;
namespace sys = boost::system;

namespace {

sys::error_code exception_error() noexcept
{
  return make_error_code(sys::errc::io_error);
}

template<typename Executor, typename Handler, typename... Args>
void post_complete(const Executor& fallback, Handler handler, Args... args)
{
  auto executor = net::get_associated_executor(handler, fallback);
  auto allocator = net::get_associated_allocator(handler);
  auto values = std::make_tuple(std::move(args)...);
  auto completion =
      net::bind_allocator(allocator, [handler = std::move(handler), values = std::move(values)]() mutable {
        std::apply([&handler](auto&&... value) { handler(std::move(value)...); }, std::move(values));
      });
  net::post(std::move(fallback), net::bind_allocator(allocator, [executor = std::move(executor),
                                                                 completion = std::move(completion)]() mutable {
              net::dispatch(std::move(executor), std::move(completion));
            }));
}

template<typename Executor, typename Handler, typename... Args>
void complete(const Executor& fallback, Handler handler, Args... args)
{
  auto executor = net::get_associated_executor(handler, fallback);
  auto allocator = net::get_associated_allocator(handler);
  auto values = std::make_tuple(std::move(args)...);
  net::dispatch(std::move(executor),
                net::bind_allocator(allocator, [handler = std::move(handler), values = std::move(values)]() mutable {
                  std::apply([&handler](auto&&... value) { handler(std::move(value)...); }, std::move(values));
                }));
}

class body_reader final : public std::enable_shared_from_this<body_reader> {
public:
  enum class mode { buffered, streamed, discarded };

  body_reader(details::request_state& state, mode value, void* consumer, request_context::body_chunk_callback callback)
    : m_state{state}
    , m_mode{value}
    , m_consumer{consumer}
    , m_callback{callback}
  {}

  void start(completion_handler handler)
  {
    m_handler = std::move(handler);
    read();
  }

private:
  void read()
  {
    if (m_state.parser.is_done()) {
      if (m_mode == mode::buffered) m_state.body_access = details::request_body_access::buffered;
      finish({});
      return;
    }

    auto& body = m_state.parser.get().body();
    body.data = m_chunk.data();
    body.size = m_chunk.size();
    auto self = shared_from_this();
    m_state.io.async_read_some(m_state.buffer, m_state.parser,
                               [self](sys::error_code ec) { self->read_completed(ec); });
  }

  void read_completed(sys::error_code ec)
  {
    if (ec) {
      if (ec == http::error::body_limit)
        m_state.exception =
            std::make_exception_ptr(request_error{http::status::payload_too_large, "Payload Too Large"});
      finish(ec);
      return;
    }

    auto& body = m_state.parser.get().body();
    const std::size_t used = m_chunk.size() - body.size;
    if (used != 0) {
      const byte_view bytes{m_chunk.data(), used};
      details::observe_request_body(m_state, bytes);
      if (m_mode == mode::buffered) {
        m_state.body.append(m_chunk.data(), used);
      } else if (m_mode == mode::streamed) {
        std::exception_ptr failure;
        try {
          m_callback(m_consumer, std::string_view{m_chunk.data(), used});
        } catch (...) {
          failure = std::current_exception();
        }
        if (failure) {
          m_state.exception = std::move(failure);
          finish(exception_error());
          return;
        }
      }
    }
    read();
  }

  void finish(sys::error_code ec)
  {
    completion_handler handler = std::move(m_handler);
    complete(m_state.io.get_executor(), std::move(handler), ec);
  }

  details::request_state& m_state;
  mode m_mode;
  void* m_consumer{};
  request_context::body_chunk_callback m_callback{};
  std::array<char, 8192> m_chunk{};
  completion_handler m_handler;
};

template<typename Response>
std::optional<Response>& buffered_response(details::request_state& state);

template<>
std::optional<details::string_response>& buffered_response<details::string_response>(details::request_state& state)
{
  return state.string_response_message;
}

template<>
std::optional<details::byte_response>& buffered_response<details::byte_response>(details::request_state& state)
{
  return state.byte_response_message;
}

template<typename Response>
void finish_buffered_send(details::request_state& state, sys::error_code ec)
{
  completion_handler handler = std::move(*state.response_completion);
  state.response_completion.reset();
  buffered_response<Response>(state).reset();
  complete(state.io.get_completion_executor(), std::move(handler), ec);
}

template<typename Response>
void buffered_send_written(details::request_state& state, sys::error_code ec, bool skip_body)
{
  auto& res = *buffered_response<Response>(state);
  if (!ec) {
    if (!skip_body) details::observe_response_body(state, byte_view{res.body()});
    state.complete_response();
  }
  finish_buffered_send<Response>(state, ec);
}

template<typename Response>
void write_buffered_response(details::request_state& state, sys::error_code ec)
{
  if (ec) {
    finish_buffered_send<Response>(state, ec);
    return;
  }

  auto& res = *buffered_response<Response>(state);
  const bool skip_body = state.parser.get().method() == http::verb::head;
  state.start_response(res.result(), res.body().size());
  details::observe_response_headers(state, res.result(), res.base(),
                                    res.body().empty() ? middleware::response_body_kind::empty
                                                       : middleware::response_body_kind::buffered,
                                    res.body().size());
  try {
    state.io.async_write(res, skip_body, [&state, skip_body](sys::error_code write_ec) {
      buffered_send_written<Response>(state, write_ec, skip_body);
    });
    return;
  } catch (...) {
    state.exception = std::current_exception();
    state.force_close = true;
  }
  finish_buffered_send<Response>(state, exception_error());
}

template<typename Response>
void start_buffered_send(details::request_state& state, http::status status,
                         typename Response::body_type::value_type body, std::string_view content_type,
                         completion_handler handler)
{
  auto& res = buffered_response<Response>(state).emplace(status, state.parser.get().version());
  for (const auto& field : state.response_fields)
    res.insert(field.name_string(), field.value());
  if (!content_type.empty()) res.set(http::field::content_type, content_type);
  res.keep_alive(state.parser.get().keep_alive() && !state.force_close);
  res.body() = std::move(body);
  res.prepare_payload();
  state.response_completion.emplace(std::move(handler));

  if (state.parser.is_done() || state.force_close) {
    write_buffered_response<Response>(state, {});
    return;
  }
  request_context{state}.do_async_discard_body(
      [&state](sys::error_code ec) { write_buffered_response<Response>(state, ec); });
}

class chunked_sender final : public std::enable_shared_from_this<chunked_sender> {
public:
  chunked_sender(details::request_state& state, http::status status, std::string content_type, void* producer,
                 request_context::chunked_response_callback callback, completion_handler handler)
    : m_state{state}
    , m_status{status}
    , m_content_type{std::move(content_type)}
    , m_producer{producer}
    , m_callback{callback}
    , m_handler{std::move(handler)}
    , m_writer{state}
  {}

  void start()
  {
    if (m_state.parser.is_done() || m_state.force_close) {
      body_discarded({});
      return;
    }
    auto self = shared_from_this();
    request_context{m_state}.do_async_discard_body([self](sys::error_code ec) { self->body_discarded(ec); });
  }

private:
  void body_discarded(sys::error_code ec)
  {
    if (ec) return finish(ec);
    m_response.emplace(m_status, m_state.parser.get().version());
    for (const auto& field : m_state.response_fields)
      m_response->insert(field.name_string(), field.value());
    if (!m_content_type.empty()) m_response->set(http::field::content_type, m_content_type);
    m_response->keep_alive(m_state.parser.get().keep_alive() && !m_state.force_close);
    m_response->chunked(true);
    m_skip_body = m_state.parser.get().method() == http::verb::head;
    m_state.start_response(m_status, 0);
    details::observe_response_headers(m_state, m_status, m_response->base(), middleware::response_body_kind::chunked,
                                      std::nullopt);
    auto self = shared_from_this();
    try {
      m_state.io.async_write_chunked_header(*m_response, m_skip_body,
                                            [self](sys::error_code writeEc) { self->header_written(writeEc); });
      return;
    } catch (...) {
      m_state.exception = std::current_exception();
      m_state.force_close = true;
    }
    finish(exception_error());
  }

  void header_written(sys::error_code ec)
  {
    if (ec) return finish(ec);
    if (m_skip_body) {
      m_state.complete_response();
      return finish({});
    }
    auto self = shared_from_this();
    try {
      m_callback(m_producer, m_writer, [self](sys::error_code produceEc) { self->produced(produceEc); });
      return;
    } catch (...) {
      m_state.exception = std::current_exception();
      m_state.force_close = true;
    }
    finish(exception_error());
  }

  void produced(sys::error_code ec)
  {
    if (ec) {
      m_state.force_close = true;
      return finish(ec);
    }
    auto self = shared_from_this();
    try {
      m_state.io.async_write_chunk_last([self](sys::error_code writeEc) {
        if (!writeEc) self->m_state.complete_response();
        self->finish(writeEc);
      });
      return;
    } catch (...) {
      m_state.exception = std::current_exception();
      m_state.force_close = true;
    }
    finish(exception_error());
  }

  void finish(sys::error_code ec)
  {
    complete(m_state.io.get_executor(), std::move(m_handler), ec);
  }

  details::request_state& m_state;
  http::status m_status;
  std::string m_content_type;
  void* m_producer;
  request_context::chunked_response_callback m_callback;
  completion_handler m_handler;
  response_writer m_writer;
  std::optional<details::empty_response> m_response;
  bool m_skip_body{};
};

class file_sender final : public std::enable_shared_from_this<file_sender> {
public:
  file_sender(details::request_state& state, http::status status, std::filesystem::path path, std::uint64_t offset,
              std::optional<std::uint64_t> length, std::string content_type, request_context::bool_handler handler)
    : m_state{state}
    , m_status{status}
    , m_path{std::move(path)}
    , m_offset{offset}
    , m_length{length}
    , m_content_type{std::move(content_type)}
    , m_handler{std::move(handler)}
  {}

  void start()
  {
    sys::error_code fileEc;
    const std::string path_text = m_path.string();
    if (m_length) {
      m_file.open(path_text.c_str(), boost::beast::file_mode::scan, fileEc);
      if (!fileEc) {
        const std::uint64_t size = m_file.size(fileEc);
        if (!fileEc && (m_offset >= size || *m_length > size - m_offset))
          fileEc = make_error_code(sys::errc::invalid_argument);
      }
      if (!fileEc) m_file.seek(m_offset, fileEc);
    } else {
      m_file_response.emplace(m_status, m_state.parser.get().version());
      m_file_response->body().open(path_text.c_str(), boost::beast::file_mode::scan, fileEc);
    }
    if (fileEc) {
      post_complete(m_state.io.get_executor(), std::move(m_handler), sys::error_code{}, false);
      return;
    }
    if (m_state.parser.is_done() || m_state.force_close) {
      body_discarded({});
      return;
    }
    auto self = shared_from_this();
    request_context{m_state}.do_async_discard_body([self](sys::error_code ec) { self->body_discarded(ec); });
  }

private:
  void body_discarded(sys::error_code ec)
  {
    if (ec) return finish(ec, false);
    const bool skip_body = m_state.parser.get().method() == http::verb::head;
    if (m_length) {
      m_empty_response.emplace(m_status, m_state.parser.get().version());
      prepare(m_empty_response->base());
      m_empty_response->keep_alive(m_state.parser.get().keep_alive() && !m_state.force_close);
      m_empty_response->content_length(*m_length);
      m_state.start_response(m_status, static_cast<std::size_t>(*m_length));
      details::observe_response_headers(m_state, m_status, m_empty_response->base(),
                                        middleware::response_body_kind::file, *m_length);
      auto self = shared_from_this();
      try {
        m_state.io.async_write_file_range(
            *m_empty_response, m_file, *m_length, skip_body,
            [self, skip_body](sys::error_code writeEc) { self->written(writeEc, skip_body); });
        return;
      } catch (...) {
        m_state.exception = std::current_exception();
        m_state.force_close = true;
      }
      finish(exception_error(), false);
    } else {
      prepare(m_file_response->base());
      m_file_response->keep_alive(m_state.parser.get().keep_alive() && !m_state.force_close);
      m_file_response->prepare_payload();
      const std::uint64_t size = m_file_response->body().size();
      m_state.start_response(m_status, static_cast<std::size_t>(size));
      details::observe_response_headers(m_state, m_status, m_file_response->base(),
                                        middleware::response_body_kind::file, size);
      auto self = shared_from_this();
      try {
        m_state.io.async_write(*m_file_response, skip_body,
                               [self, skip_body](sys::error_code writeEc) { self->written(writeEc, skip_body); });
        return;
      } catch (...) {
        m_state.exception = std::current_exception();
        m_state.force_close = true;
      }
      finish(exception_error(), false);
    }
  }

  void prepare(http::fields& fields)
  {
    for (const auto& field : m_state.response_fields)
      fields.insert(field.name_string(), field.value());
    if (!m_content_type.empty()) fields.set(http::field::content_type, m_content_type);
  }

  void written(sys::error_code ec, bool skip_body)
  {
    if (!ec) {
      const std::uint64_t size = m_length ? *m_length : m_file_response->body().size();
      if (!skip_body) details::observe_response_file(m_state, m_path, m_offset, size);
      m_state.complete_response();
    }
    finish(ec, !ec);
  }

  void finish(sys::error_code ec, bool sent)
  {
    complete(m_state.io.get_executor(), std::move(m_handler), ec, sent);
  }

  details::request_state& m_state;
  http::status m_status;
  std::filesystem::path m_path;
  std::uint64_t m_offset;
  std::optional<std::uint64_t> m_length;
  std::string m_content_type;
  request_context::bool_handler m_handler;
  boost::beast::file m_file;
  std::optional<details::file_response> m_file_response;
  std::optional<details::empty_response> m_empty_response;
};

} // namespace

request_context::request_context(details::request_state& state) noexcept
  : m_state{&state}
{}

http::verb request_context::method() const noexcept
{
  return m_state->parser.get().method();
}
unsigned request_context::version() const noexcept
{
  return m_state->parser.get().version();
}
bool request_context::keep_alive() const noexcept
{
  return m_state->parser.get().keep_alive() && !m_state->force_close;
}
target_view request_context::target() const noexcept
{
  return m_state->parsed_target;
}
std::string_view request_context::matched_route() const noexcept
{
  return m_state->current_route;
}
std::uint64_t request_context::listener_id() const noexcept
{
  return m_state->listener_id;
}
const serveza::connection_info* request_context::connection() const noexcept
{
  return m_state->connection;
}
const request_context::fields_type& request_context::request_headers() const noexcept
{
  return m_state->parser.get().base();
}
const web::route_params& request_context::params() const noexcept
{
  return m_state->route_values;
}
std::optional<std::string_view> request_context::param(std::string_view name) const noexcept
{
  return params().find(name);
}
web::storage& request_context::storage() noexcept
{
  return m_state->request_storage;
}
const web::storage& request_context::storage() const noexcept
{
  return m_state->request_storage;
}
request_context::fields_type& request_context::response_headers() noexcept
{
  return m_state->response_fields;
}
const request_context::fields_type& request_context::response_headers() const noexcept
{
  return m_state->response_fields;
}
net::any_io_executor request_context::get_executor() const
{
  return m_state->io.get_executor();
}

std::optional<std::uint64_t> request_context::content_length() const noexcept
{
  const auto value = m_state->parser.content_length();
  return value ? std::optional<std::uint64_t>{*value} : std::nullopt;
}

std::size_t request_context::maximum_body_size() const noexcept
{
  return m_state->config.body_limit;
}

void request_context::set_body_limit(std::size_t limit)
{
  if (m_state->body_access != details::request_body_access::untouched)
    throw std::logic_error{"cannot change body limit after body consumption has started"};
  if (limit > maximum_body_size()) throw std::invalid_argument{"request body limit exceeds application maximum"};
  if (const auto length = content_length(); length && *length > limit)
    throw request_error{http::status::payload_too_large, "Payload Too Large"};
  m_state->parser.body_limit(static_cast<std::uint64_t>(limit));
}

bool request_context::body_consumed() const noexcept
{
  return m_state->parser.is_done();
}
std::size_t request_context::request_body_size() const noexcept
{
  return m_state->request_body_size;
}

void request_context::validate_read_body() const
{
  if (m_state->body_access == details::request_body_access::streamed)
    throw std::logic_error{"request body was already consumed as a stream"};
  if (m_state->response != details::response_progress::idle)
    throw std::logic_error{"cannot read body after response commit"};
}

void request_context::validate_read_body_chunks() const
{
  if (m_state->body_access != details::request_body_access::untouched)
    throw std::logic_error{"request body was already consumed"};
  if (m_state->response != details::response_progress::idle)
    throw std::logic_error{"cannot read body after response commit"};
}

void request_context::validate_discard_body() const
{
  if (!m_state->parser.is_done() && !m_state->force_close && m_state->response != details::response_progress::idle)
    throw std::logic_error{"cannot discard body after response commit"};
}

void request_context::validate_send() const
{
  if (response_committed()) throw std::logic_error{"HTTP response is already committed"};
}

void request_context::validate_send_chunked(http::status status) const
{
  validate_send();
  if (version() < 11) throw std::logic_error{"chunked responses require HTTP/1.1 or newer"};
  if (status == http::status::no_content || status == http::status::not_modified ||
      static_cast<unsigned>(status) < 200U)
    throw std::invalid_argument{"response status does not permit a chunked body"};
}

void request_context::validate_file_range(std::uint64_t length)
{
  if (length == 0) throw std::invalid_argument{"file response range must not be empty"};
  if (length > std::numeric_limits<std::size_t>::max())
    throw std::length_error{"file response range is too large for this platform"};
}

void request_context::validate_websocket_accept(const websocket_options& options) const
{
  validate_send();
  if (!websocket_upgrade_requested()) throw std::logic_error{"request is not a WebSocket upgrade"};
  details::validate_websocket_options(options);
}

void request_context::do_async_read_body(body_handler handler)
{
  if (m_state->body_access == details::request_body_access::buffered) {
    post_complete(get_executor(), std::move(handler), sys::error_code{}, std::string_view{m_state->body});
    return;
  }
  if (m_state->body_access == details::request_body_access::streamed)
    throw std::logic_error{"request body was already consumed as a stream"};
  if (m_state->response != details::response_progress::idle)
    throw std::logic_error{"cannot read body after response commit"};
  auto reader = std::make_shared<body_reader>(*m_state, body_reader::mode::buffered, nullptr, nullptr);
  reader->start([this, reader, handler = std::move(handler)](sys::error_code ec) mutable {
    complete(get_executor(), std::move(handler), ec, std::string_view{m_state->body});
  });
}

void request_context::do_async_read_body_bytes(byte_body_handler handler)
{
  do_async_read_body([this, handler = std::move(handler)](sys::error_code ec, std::string_view body) mutable {
    complete(get_executor(), std::move(handler), ec, byte_view{body});
  });
}

void request_context::do_async_read_body_chunks(void* consumer, body_chunk_callback callback,
                                                completion_handler handler)
{
  if (m_state->body_access != details::request_body_access::untouched)
    throw std::logic_error{"request body was already consumed"};
  if (m_state->response != details::response_progress::idle)
    throw std::logic_error{"cannot read body after response commit"};
  m_state->body_access = details::request_body_access::streamed;
  std::make_shared<body_reader>(*m_state, body_reader::mode::streamed, consumer, callback)->start(std::move(handler));
}

void request_context::do_async_discard_body(completion_handler handler)
{
  if (m_state->parser.is_done() || m_state->force_close) {
    post_complete(get_executor(), std::move(handler), sys::error_code{});
    return;
  }
  if (m_state->response != details::response_progress::idle)
    throw std::logic_error{"cannot discard body after response commit"};
  std::make_shared<body_reader>(*m_state, body_reader::mode::discarded, nullptr, nullptr)->start(std::move(handler));
}

bool request_context::response_committed() const noexcept
{
  return m_state->response != details::response_progress::idle;
}
std::optional<http::status> request_context::response_status() const noexcept
{
  return m_state->response_status;
}
std::size_t request_context::response_body_size() const noexcept
{
  return m_state->response_body_size;
}
void request_context::close_after_response() noexcept
{
  m_state->force_close = true;
}

void request_context::do_async_send(http::status status, std::string body, std::string_view content_type,
                                    completion_handler handler)
{
  if (response_committed()) throw std::logic_error{"HTTP response is already committed"};
  start_buffered_send<details::string_response>(*m_state, status, std::move(body), content_type, std::move(handler));
}

void request_context::do_async_send(http::status status, byte_buffer body, std::string_view content_type,
                                    completion_handler handler)
{
  if (response_committed()) throw std::logic_error{"HTTP response is already committed"};
  start_buffered_send<details::byte_response>(*m_state, status, std::move(body), content_type, std::move(handler));
}

void request_context::do_async_send_chunked(http::status status, std::string content_type, void* producer,
                                            chunked_response_callback callback, completion_handler handler)
{
  if (response_committed()) throw std::logic_error{"HTTP response is already committed"};
  if (version() < 11) throw std::logic_error{"chunked responses require HTTP/1.1 or newer"};
  if (status == http::status::no_content || status == http::status::not_modified ||
      static_cast<unsigned>(status) < 200U)
    throw std::invalid_argument{"response status does not permit a chunked body"};
  std::make_shared<chunked_sender>(*m_state, status, std::move(content_type), producer, callback, std::move(handler))
      ->start();
}

void request_context::do_async_send_file(http::status status, std::filesystem::path path, std::string content_type,
                                         bool_handler handler)
{
  if (response_committed()) throw std::logic_error{"HTTP response is already committed"};
  std::make_shared<file_sender>(*m_state, status, std::move(path), 0, std::nullopt, std::move(content_type),
                                std::move(handler))
      ->start();
}

void request_context::do_async_send_file_range(http::status status, std::filesystem::path path, std::uint64_t offset,
                                               std::uint64_t length, std::string content_type, bool_handler handler)
{
  if (response_committed()) throw std::logic_error{"HTTP response is already committed"};
  if (length == 0) throw std::invalid_argument{"file response range must not be empty"};
  if (length > std::numeric_limits<std::size_t>::max())
    throw std::length_error{"file response range is too large for this platform"};
  std::make_shared<file_sender>(*m_state, status, std::move(path), offset, length, std::move(content_type),
                                std::move(handler))
      ->start();
}

bool request_context::websocket_upgrade_requested() const noexcept
{
  return boost::beast::websocket::is_upgrade(m_state->parser.get());
}

void request_context::do_async_accept_websocket(void* endpoint, websocket_callback callback, websocket_options options,
                                                completion_handler handler)
{
  if (response_committed()) throw std::logic_error{"HTTP response is already committed"};
  if (!websocket_upgrade_requested()) throw std::logic_error{"request is not a WebSocket upgrade"};
  details::validate_websocket_options(options);
  m_state->start_response(http::status::switching_protocols, 0);
  m_state->io.async_accept_websocket(m_state->parser, m_state->response_fields, options, endpoint, callback,
                                     std::move(handler));
}

std::exception_ptr request_context::take_exception() noexcept
{
  return std::exchange(m_state->exception, {});
}

void request_context::report_exception(std::exception_ptr ep) noexcept
{
  m_state->exception = std::move(ep);
}

} // namespace serveza::web
