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

#include <serveza/web/session.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/system_error.hpp>

#include <serveza/web/errors.h>

#if SERVEZA_WEB_USE_SSL
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/websocket/ssl.hpp>
#endif

#include "detail/core.h"

namespace serveza::web {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace sys = boost::system;

namespace {

template<typename Handler, typename... Args>
void complete(net::any_io_executor fallback, Handler handler, Args... args)
{
  auto executor = net::get_associated_executor(handler, fallback);
  auto allocator = net::get_associated_allocator(handler);
  auto values = std::make_tuple(std::move(args)...);
  net::dispatch(std::move(executor),
                net::bind_allocator(allocator, [handler = std::move(handler), values = std::move(values)]() mutable {
                  std::apply([&handler](auto&&... value) { handler(std::move(value)...); }, std::move(values));
                }));
}

template<typename Handler, typename... Args>
void post_complete(net::any_io_executor fallback, Handler handler, Args... args)
{
  auto executor = net::get_associated_executor(handler, fallback);
  auto allocator = net::get_associated_allocator(handler);
  auto values = std::make_tuple(std::move(args)...);
  net::post(std::move(fallback),
            net::bind_allocator(allocator, [executor = std::move(executor), allocator, handler = std::move(handler),
                                            values = std::move(values)]() mutable {
              net::dispatch(
                  std::move(executor),
                  net::bind_allocator(allocator, [handler = std::move(handler), values = std::move(values)]() mutable {
                    std::apply([&handler](auto&&... value) { handler(std::move(value)...); }, std::move(values));
                  }));
            }));
}

bool expected_disconnect(const sys::error_code& ec) noexcept
{
  return ec == http::error::end_of_stream || ec == net::error::eof || ec == net::error::connection_reset ||
         ec == net::error::connection_aborted || ec == net::error::broken_pipe || ec == net::error::operation_aborted;
}

class socket_deadline final {
public:
  explicit socket_deadline(net::ip::tcp::socket& socket)
    : m_socket{socket}
    , m_timer{socket.get_executor()}
  {}

  ~socket_deadline()
  {
    cancel();
  }

  void arm(std::chrono::seconds timeout)
  {
    m_timer.expires_after(timeout);
    net::ip::tcp::socket* socket = &m_socket;
    m_timer.async_wait([socket](sys::error_code ec) {
      if (ec || !socket->is_open()) return;
      sys::error_code ignored;
      socket->cancel(ignored);
      socket->close(ignored);
    });
  }

  void cancel() noexcept
  {
    try {
      m_timer.cancel();
    } catch (...) {}
  }

private:
  net::ip::tcp::socket& m_socket;
  net::steady_timer m_timer;
};

template<typename Stream>
class beast_websocket_io final : public details::websocket_io,
                                 public std::enable_shared_from_this<beast_websocket_io<Stream>> {
  struct outgoing_message final {
    using payload_type = std::variant<std::string, byte_buffer>;
    payload_type payload;
    std::shared_ptr<completion_handler> completion;

    [[nodiscard]] bool is_text() const noexcept
    {
      return std::holds_alternative<std::string>(payload);
    }
    [[nodiscard]] std::size_t size() const noexcept
    {
      return std::visit([](const auto& value) { return value.size(); }, payload);
    }
    [[nodiscard]] net::const_buffer buffer() const noexcept
    {
      return std::visit([](const auto& value) { return net::buffer(value.data(), value.size()); }, payload);
    }
  };

public:
  beast_websocket_io(Stream& stream, const websocket_options& options)
    : m_stream{stream}
    , m_max_message_size{options.max_message_size}
    , m_close_timeout{options.close_timeout}
    , m_max_pending_messages{options.max_pending_messages}
    , m_max_pending_bytes{options.max_pending_bytes}
    , m_close_deadline{stream.get_executor()}
  {
    auto timeout = websocket::stream_base::timeout::suggested(beast::role_type::server);
    timeout.handshake_timeout = options.handshake_timeout;
    timeout.idle_timeout = options.idle_timeout;
    timeout.keep_alive_pings = options.keep_alive_pings;
    m_stream.set_option(timeout);
    m_stream.read_message_max(options.max_message_size);
  }

  [[nodiscard]] net::any_io_executor get_executor() override
  {
    return m_stream.get_executor();
  }

  void async_accept(const details::parser_type& parser, const http::fields& response_fields, completion_handler handler)
  {
    m_stream.set_option(websocket::stream_base::decorator([&response_fields](websocket::response_type& response) {
      for (const auto& field : response_fields)
        response.insert(field.name_string(), field.value());
    }));
    m_stream.async_accept(parser.get(),
                          [self = this->shared_from_this(), handler = std::move(handler)](sys::error_code ec) mutable {
                            complete(self->get_executor(), std::move(handler), ec);
                          });
  }

  [[nodiscard]] bool is_open() const noexcept override
  {
    std::lock_guard lock{m_mutex};
    return m_accepting && m_open.load(std::memory_order_relaxed);
  }

  void async_read(read_handler handler) override
  {
    struct read_state final {
      explicit read_state(std::size_t limit, read_handler value)
        : buffer{limit}
        , handler{std::move(value)}
      {}
      beast::flat_buffer buffer;
      read_handler handler;
    };
    auto state = std::make_shared<read_state>(m_max_message_size, std::move(handler));
    m_stream.async_read(state->buffer, [self = this->shared_from_this(), state](sys::error_code ec, std::size_t) {
      if (ec == websocket::error::closed) {
        self->mark_closed();
        complete(self->get_executor(), std::move(state->handler), sys::error_code{}, std::nullopt);
        return;
      }
      if (ec) {
        self->mark_closed();
        complete(self->get_executor(), std::move(state->handler), ec, std::nullopt);
        return;
      }
      byte_buffer data(state->buffer.size());
      net::buffer_copy(net::buffer(data), state->buffer.data());
      std::optional<websocket_message> message{websocket_message{
          std::move(data), self->m_stream.got_text() ? websocket_message_type::text : websocket_message_type::binary}};
      complete(self->get_executor(), std::move(state->handler), sys::error_code{}, std::move(message));
    });
  }

  [[nodiscard]] websocket_send_result try_send_text(std::string value) override
  {
    return enqueue(outgoing_message{std::move(value), {}});
  }

  [[nodiscard]] websocket_send_result try_send_binary(byte_buffer value) override
  {
    return enqueue(outgoing_message{std::move(value), {}});
  }

  void async_write_text(std::string value, completion_handler handler) override
  {
    enqueue_with_completion(
        outgoing_message{std::move(value), std::make_shared<completion_handler>(std::move(handler))});
  }

  void async_write_binary(byte_buffer value, completion_handler handler) override
  {
    enqueue_with_completion(
        outgoing_message{std::move(value), std::make_shared<completion_handler>(std::move(handler))});
  }

  void async_ping(std::string value, completion_handler handler) override
  {
    if (value.size() > 125) throw std::length_error{"WebSocket ping payload exceeds 125 bytes"};
    auto payload = std::make_shared<std::string>(std::move(value));
    m_stream.async_ping(websocket::ping_data{payload->data(), payload->size()},
                        [self = this->shared_from_this(), payload, handler = std::move(handler)](
                            sys::error_code ec) mutable { complete(self->get_executor(), std::move(handler), ec); });
  }

  void async_close(std::uint16_t code, std::string reason, completion_handler handler) override
  {
    if (reason.size() > 123) throw std::length_error{"WebSocket close reason exceeds 123 bytes"};
    {
      std::lock_guard lock{m_mutex};
      if (m_close_handler) throw std::logic_error{"WebSocket close is already in progress"};
      m_accepting = false;
      m_close_code = code;
      m_close_reason = std::move(reason);
      m_close_handler.emplace(std::move(handler));
    }
    arm_close_deadline();
    net::post(m_stream.get_executor(), [self = this->shared_from_this()] { self->maybe_close(); });
  }

  void async_finish(completion_handler handler)
  {
    if (!m_stream.is_open()) {
      mark_closed();
      post_complete(get_executor(), std::move(handler), sys::error_code{});
      return;
    }
    async_close(static_cast<std::uint16_t>(websocket::close_code::normal), {}, std::move(handler));
  }

  void async_abort(sys::error_code ec, completion_handler handler) noexcept
  {
    abort_socket(ec ? ec : net::error::operation_aborted);
    post_complete(get_executor(), std::move(handler), ec);
  }

private:
  [[nodiscard]] websocket_send_result enqueue(outgoing_message message)
  {
    bool schedule = false;
    {
      std::lock_guard lock{m_mutex};
      const std::size_t size = message.size();
      if (!m_accepting || !m_open.load(std::memory_order_relaxed)) return websocket_send_result::closed;
      if (m_queue.size() >= m_max_pending_messages || size > m_max_pending_bytes - m_pending_bytes)
        return websocket_send_result::queue_full;
      m_pending_bytes += size;
      m_queue.push_back(std::move(message));
      if (!m_write_in_progress && !m_pump_scheduled) {
        m_pump_scheduled = true;
        schedule = true;
      }
    }
    if (schedule) net::post(m_stream.get_executor(), [self = this->shared_from_this()] { self->pump(); });
    return websocket_send_result::queued;
  }

  void enqueue_with_completion(outgoing_message message)
  {
    std::shared_ptr<completion_handler> handler = message.completion;
    const websocket_send_result result = enqueue(std::move(message));
    if (result == websocket_send_result::queued) return;
    sys::error_code ec = result == websocket_send_result::closed ? make_error_code(net::error::operation_aborted)
                                                                 : make_error_code(sys::errc::no_buffer_space);
    if (handler) post_complete(get_executor(), std::move(*handler), ec);
  }

  void pump()
  {
    {
      std::lock_guard lock{m_mutex};
      m_pump_scheduled = false;
      if (m_write_in_progress || m_queue.empty()) {
        if (m_queue.empty()) maybe_close_unlocked();
        return;
      }
      if (!m_open.load(std::memory_order_relaxed)) {
        fail_queue_unlocked(net::error::operation_aborted);
        maybe_close_unlocked();
        return;
      }
      m_write_in_progress = true;
      m_stream.text(m_queue.front().is_text());
    }
    m_stream.async_write(m_queue.front().buffer(), [self = this->shared_from_this()](sys::error_code ec, std::size_t) {
      self->write_completed(ec);
    });
  }

  void write_completed(sys::error_code ec)
  {
    std::shared_ptr<completion_handler> handler;
    bool continue_writing = false;
    {
      std::lock_guard lock{m_mutex};
      if (!m_queue.empty()) {
        m_pending_bytes -= m_queue.front().size();
        handler = std::move(m_queue.front().completion);
        m_queue.pop_front();
      }
      m_write_in_progress = false;
      if (ec) {
        m_open.store(false, std::memory_order_release);
        m_accepting = false;
        fail_queue_unlocked(ec);
      } else if (!m_queue.empty()) {
        continue_writing = true;
      }
    }
    if (handler) complete(get_executor(), std::move(*handler), ec);
    if (continue_writing)
      pump();
    else
      maybe_close();
  }

  void maybe_close()
  {
    std::optional<completion_handler> handler;
    websocket::close_reason reason;
    {
      std::lock_guard lock{m_mutex};
      if (m_write_in_progress || !m_queue.empty() || !m_close_handler) return;
      if (!m_stream.is_open()) {
        handler = std::move(m_close_handler);
        m_close_handler.reset();
      } else {
        reason = websocket::close_reason{static_cast<websocket::close_code>(m_close_code),
                                         beast::string_view{m_close_reason.data(), m_close_reason.size()}};
      }
    }
    if (handler) {
      cancel_close_deadline();
      mark_closed();
      complete(get_executor(), std::move(*handler), sys::error_code{});
      return;
    }
    m_stream.async_close(reason, [self = this->shared_from_this()](sys::error_code ec) {
      if (ec == websocket::error::closed || ec == net::error::eof || ec == net::error::operation_aborted) ec.clear();
      self->close_completed(ec);
    });
  }

  void maybe_close_unlocked()
  {
    if (m_close_handler) net::post(m_stream.get_executor(), [self = this->shared_from_this()] { self->maybe_close(); });
  }

  void close_completed(sys::error_code ec)
  {
    std::optional<completion_handler> handler;
    {
      std::lock_guard lock{m_mutex};
      handler = std::move(m_close_handler);
      m_close_handler.reset();
    }
    cancel_close_deadline();
    mark_closed();
    if (handler) complete(get_executor(), std::move(*handler), ec);
  }

  void fail_queue_unlocked(sys::error_code ec)
  {
    std::vector<std::shared_ptr<completion_handler>> handlers;
    for (auto& message : m_queue) {
      if (message.completion) handlers.push_back(std::move(message.completion));
    }
    m_queue.clear();
    m_pending_bytes = 0;
    for (auto& handler : handlers)
      post_complete(get_executor(), std::move(*handler), ec);
  }

  void mark_closed() noexcept
  {
    m_open.store(false, std::memory_order_release);
    std::lock_guard lock{m_mutex};
    m_accepting = false;
  }

  void arm_close_deadline()
  {
    m_close_deadline.expires_after(m_close_timeout);
    m_close_deadline.async_wait([weak = this->weak_from_this()](sys::error_code ec) {
      if (ec) return;
      if (auto self = weak.lock()) self->abort_socket(net::error::timed_out);
    });
  }

  void cancel_close_deadline() noexcept
  {
    try {
      m_close_deadline.cancel();
    } catch (...) {}
  }

  void abort_socket(sys::error_code ec) noexcept
  {
    std::optional<completion_handler> close_handler;
    {
      std::lock_guard lock{m_mutex};
      m_accepting = false;
      m_open.store(false, std::memory_order_release);
      fail_queue_unlocked(ec);
      close_handler = std::move(m_close_handler);
      m_close_handler.reset();
    }
    sys::error_code ignored;
    auto& socket = beast::get_lowest_layer(m_stream);
    socket.cancel(ignored);
    socket.close(ignored);
    if (close_handler) post_complete(get_executor(), std::move(*close_handler), ec);
  }

  websocket::stream<Stream&> m_stream;
  std::size_t m_max_message_size;
  std::chrono::seconds m_close_timeout;
  std::size_t m_max_pending_messages;
  std::size_t m_max_pending_bytes;
  mutable std::mutex m_mutex;
  std::deque<outgoing_message> m_queue;
  std::size_t m_pending_bytes{};
  bool m_accepting{true};
  bool m_pump_scheduled{};
  bool m_write_in_progress{};
  std::atomic_bool m_open{true};
  net::steady_timer m_close_deadline;
  std::optional<completion_handler> m_close_handler;
  std::uint16_t m_close_code{};
  std::string m_close_reason;
};

template<typename Stream>
class beast_transport final : public details::transport {
public:
  beast_transport(Stream& stream, socket_deadline& deadline, std::chrono::seconds timeout)
    : m_stream{stream}
    , m_deadline{deadline}
    , m_timeout{timeout}
    , m_completion_executor{stream.get_executor()}
  {}

  [[nodiscard]] net::any_io_executor get_executor() override
  {
    return m_stream.get_executor();
  }

  [[nodiscard]] const net::any_completion_executor& get_completion_executor() const noexcept override
  {
    return m_completion_executor;
  }
  void set_timeout(std::chrono::seconds value) noexcept
  {
    m_timeout = value;
  }

  void async_read_header(beast::flat_buffer& buffer, details::parser_type& parser,
                         details::completion_callback handler) override
  {
    m_deadline.arm(m_timeout);
    http::async_read_header(m_stream, buffer, parser,
                            [this, handler = std::move(handler)](sys::error_code ec, std::size_t) mutable {
                              m_deadline.cancel();
                              handler(ec);
                            });
  }

  void async_read_some(beast::flat_buffer& buffer, details::parser_type& parser,
                       details::completion_callback handler) override
  {
    m_deadline.arm(m_timeout);
    http::async_read_some(m_stream, buffer, parser,
                          [this, handler = std::move(handler)](sys::error_code ec, std::size_t) mutable {
                            m_deadline.cancel();
                            handler(ec);
                          });
  }

  void async_write(details::string_response& res, bool skip_body, details::completion_callback handler) override
  {
    async_write_response(res, skip_body, std::move(handler));
  }
  void async_write(details::byte_response& res, bool skip_body, details::completion_callback handler) override
  {
    async_write_response(res, skip_body, std::move(handler));
  }
  void async_write(details::file_response& res, bool skip_body, details::completion_callback handler) override
  {
    async_write_response(res, skip_body, std::move(handler));
  }

  void async_write_file_range(details::empty_response& res, beast::file& file, std::uint64_t length, bool skip_body,
                              details::completion_callback handler) override
  {
    struct state final : std::enable_shared_from_this<state> {
      state(Stream& stream_value, socket_deadline& deadline_value, beast::file& file_value, std::uint64_t length_value,
            details::completion_callback handler_value)
        : stream{stream_value}
        , deadline{deadline_value}
        , file{file_value}
        , remaining{length_value}
        , handler{std::move(handler_value)}
      {}
      void write()
      {
        if (remaining == 0) return finish({});
        sys::error_code readEc;
        const std::size_t requested = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        const std::size_t size = file.read(buffer.data(), requested, readEc);
        if (!readEc && size == 0) readEc = make_error_code(sys::errc::io_error);
        if (readEc) return finish(readEc);
        auto self = this->shared_from_this();
        net::async_write(stream, net::buffer(buffer.data(), size), [self, size](sys::error_code writeEc, std::size_t) {
          if (writeEc) return self->finish(writeEc);
          self->remaining -= size;
          self->write();
        });
      }
      void finish(sys::error_code ec)
      {
        deadline.cancel();
        handler(ec);
      }
      Stream& stream;
      socket_deadline& deadline;
      beast::file& file;
      std::uint64_t remaining;
      details::completion_callback handler;
      std::array<char, 64 * 1024> buffer{};
    };
    m_deadline.arm(m_timeout);
    auto serializer = std::make_shared<http::response_serializer<http::empty_body>>(res);
    http::async_write_header(m_stream, *serializer,
                             [this, serializer, &file, length, skip_body,
                              handler = std::move(handler)](sys::error_code ec, std::size_t) mutable {
                               if (ec || skip_body) {
                                 m_deadline.cancel();
                                 handler(ec);
                                 return;
                               }
                               std::make_shared<state>(m_stream, m_deadline, file, length, std::move(handler))->write();
                             });
  }

  void async_write_chunked_header(details::empty_response& res, bool, details::completion_callback handler) override
  {
    m_deadline.arm(m_timeout);
    auto serializer = std::make_shared<http::response_serializer<http::empty_body>>(res);
    http::async_write_header(m_stream, *serializer,
                             [this, serializer, handler = std::move(handler)](sys::error_code ec, std::size_t) mutable {
                               m_deadline.cancel();
                               handler(ec);
                             });
  }

  void async_write_chunk(const void* data, std::size_t size, details::completion_callback handler) override
  {
    m_deadline.arm(m_timeout);
    auto chunk = std::make_shared<decltype(http::make_chunk(net::buffer(data, size)))>(
        http::make_chunk(net::buffer(data, size)));
    net::async_write(m_stream, *chunk,
                     [this, chunk, handler = std::move(handler)](sys::error_code ec, std::size_t) mutable {
                       m_deadline.cancel();
                       handler(ec);
                     });
  }

  void async_write_chunk_last(details::completion_callback handler) override
  {
    m_deadline.arm(m_timeout);
    auto last = std::make_shared<decltype(http::make_chunk_last())>(http::make_chunk_last());
    net::async_write(m_stream, *last,
                     [this, last, handler = std::move(handler)](sys::error_code ec, std::size_t) mutable {
                       m_deadline.cancel();
                       handler(ec);
                     });
  }

  void async_accept_websocket(const details::parser_type& parser, const http::fields& response_fields,
                              const websocket_options& options, void* endpoint, websocket_callback callback,
                              details::completion_callback handler) override
  {
    m_deadline.cancel();
    auto io = std::make_shared<beast_websocket_io<Stream>>(m_stream, options);
    io->async_accept(parser, response_fields,
                     [this, io, endpoint, callback, handler = std::move(handler)](sys::error_code ec) mutable {
                       if (ec) return complete(get_executor(), std::move(handler), ec);
                       auto connection = details::websocket_connection_access::make(io);
                       try {
                         callback(endpoint, *connection,
                                  [io, connection, handler = std::move(handler)](sys::error_code endpoint_ec) mutable {
                                    if (endpoint_ec) {
                                      io->async_abort(endpoint_ec, std::move(handler));
                                      return;
                                    }
                                    io->async_finish(std::move(handler));
                                  });
                       } catch (...) {
                         io->async_abort(make_error_code(sys::errc::io_error), std::move(handler));
                       }
                     });
  }

private:
  using string_serializer = http::response_serializer<typename details::string_response::body_type>;
  using byte_serializer = http::response_serializer<typename details::byte_response::body_type>;
  using file_serializer = http::response_serializer<typename details::file_response::body_type>;
  using serializer_storage = std::variant<std::monostate, string_serializer, byte_serializer, file_serializer>;

  template<typename Response>
  void async_write_response(Response& res, bool skip_body, details::completion_callback handler)
  {
    using body_type = typename Response::body_type;
    using serializer_type = http::response_serializer<body_type>;
    m_deadline.arm(m_timeout);
    auto done = [this, handler = std::move(handler)](sys::error_code ec, std::size_t) mutable {
      m_deadline.cancel();
      m_serializer.template emplace<std::monostate>();
      handler(ec);
    };
    auto& serializer = m_serializer.template emplace<serializer_type>(res);
    if (skip_body) {
      http::async_write_header(m_stream, serializer, std::move(done));
    } else {
      http::async_write(m_stream, serializer, std::move(done));
    }
  }

  Stream& m_stream;
  socket_deadline& m_deadline;
  std::chrono::seconds m_timeout;
  net::any_completion_executor m_completion_executor;
  serializer_storage m_serializer;
};

template<typename Stream>
class request_loop final : public std::enable_shared_from_this<request_loop<Stream>> {
public:
  request_loop(const details::application_impl& application, settings config, Stream& stream,
               net::ip::tcp::socket& socket, std::uint64_t listener_id, const serveza::connection_info* connection)
    : m_application{application}
    , m_config{std::move(config)}
    , m_deadline{socket}
    , m_transport{stream, m_deadline, m_config.request_timeout}
    , m_buffer{m_config.header_limit + m_config.body_limit}
    , m_listener_id{listener_id}
    , m_connection{connection}
  {}

  void start(completion_handler handler)
  {
    m_handler = std::move(handler);
    read_header();
  }

  [[nodiscard]] bool upgraded() const noexcept
  {
    return m_upgraded;
  }

private:
  void read_header()
  {
    m_parser.emplace();
    const std::uint32_t header_limit = static_cast<std::uint32_t>(
        std::min<std::size_t>(m_config.header_limit, std::numeric_limits<std::uint32_t>::max()));
    m_parser->header_limit(header_limit);
    m_parser->body_limit(static_cast<std::uint64_t>(m_config.body_limit));
    m_transport.set_timeout(m_first_request ? m_config.request_timeout : m_config.keep_alive_timeout);
    auto self = this->shared_from_this();
    m_transport.async_read_header(m_buffer, *m_parser, [self](sys::error_code ec) { self->header_read(ec); });
  }

  void header_read(sys::error_code ec)
  {
    if (ec) {
      if (expected_disconnect(ec)) return finish({});
      if (ec == http::error::header_limit)
        return write_error(nullptr, http::status::request_header_fields_too_large, "Request Header Fields Too Large",
                           {});
      if (ec == http::error::body_limit)
        return write_error(nullptr, http::status::payload_too_large, "Payload Too Large", {});
      if (ec.category() == http::make_error_code(http::error::bad_method).category())
        return write_error(nullptr, http::status::bad_request, "Bad Request", {});
      return finish(ec);
    }
    m_transport.set_timeout(m_config.request_timeout);
    target_view target;
    try {
      const auto raw = m_parser->get().target();
      target = details::parse_target(std::string_view{raw.data(), raw.size()});
    } catch (...) {
      return write_error(nullptr, http::status::bad_request, "Bad Request", std::current_exception());
    }
    m_state.emplace(m_transport, m_buffer, *m_parser, target, m_config, m_listener_id, m_connection);
    auto self = this->shared_from_this();
    try {
      m_application.dispatch(
          *m_state, [self](sys::error_code dispatchEc) { self->application_completed(dispatchEc); }, m_dispatcher);
    } catch (...) {
      m_state->exception = std::current_exception();
      application_completed(make_error_code(sys::errc::io_error));
    }
  }

  void application_completed(sys::error_code ec)
  {
    if (ec) {
      std::exception_ptr failure = m_state->exception;
      if (!failure) failure = std::make_exception_ptr(sys::system_error{ec});
      if (expected_disconnect(ec)) {
        details::complete_traffic_observers(*m_state, failure);
        return finish({});
      }
      if (m_state->response != details::response_progress::idle) {
        details::complete_traffic_observers(*m_state, failure);
        return finish(ec);
      }
      http::status status =
          ec == http::error::body_limit ? http::status::payload_too_large : http::status::internal_server_error;
      std::string body = status == http::status::payload_too_large ? "Payload Too Large" : "Internal Server Error";
      if (failure) {
        try {
          std::rethrow_exception(failure);
        } catch (const request_error& req) {
          status = req.status();
          body = req.what();
        } catch (...) {}
      }
      return write_error(&*m_state, status, std::move(body), std::move(failure));
    }

    details::complete_traffic_observers(*m_state, nullptr);
    if (m_state->websocket_upgraded) {
      m_upgraded = true;
      return finish({});
    }
    if (m_state->force_close || !m_parser->get().keep_alive()) return finish({});
    m_state.reset();
    m_parser.reset();
    m_first_request = false;
    read_header();
  }

  void write_error(details::request_state* state, http::status status, std::string body, std::exception_ptr failure)
  {
    unsigned version = m_parser ? m_parser->get().version() : 11;
    if (version != 10 && version != 11) version = 11;
    m_error_response.emplace(status, version);
    m_error_response->keep_alive(false);
    m_error_response->set(http::field::content_type, "text/plain; charset=utf-8");
    m_error_response->body() = std::move(body);
    m_error_response->prepare_payload();
    if (state) {
      state->start_response(status, m_error_response->body().size());
      details::observe_response_headers(*state, status, m_error_response->base(),
                                        middleware::response_body_kind::buffered, m_error_response->body().size());
      state->force_close = true;
    }
    auto self = this->shared_from_this();
    m_transport.async_write(*m_error_response, false,
                            [self, state, failure = std::move(failure)](sys::error_code ec) mutable {
                              if (state) {
                                if (!ec) {
                                  details::observe_response_body(*state, byte_view{self->m_error_response->body()});
                                  state->complete_response();
                                }
                                details::complete_traffic_observers(*state, failure);
                              }
                              self->finish(ec);
                            });
  }

  void finish(sys::error_code ec)
  {
    completion_handler handler = std::move(m_handler);
    complete(m_transport.get_executor(), std::move(handler), ec);
  }

  const details::application_impl& m_application;
  settings m_config;
  socket_deadline m_deadline;
  beast_transport<Stream> m_transport;
  beast::flat_buffer m_buffer;
  std::optional<details::parser_type> m_parser;
  std::optional<details::request_state> m_state;
  std::shared_ptr<details::dispatcher> m_dispatcher;
  std::optional<details::string_response> m_error_response;
  std::uint64_t m_listener_id;
  const serveza::connection_info* m_connection;
  completion_handler m_handler;
  bool m_first_request{true};
  bool m_upgraded{};
};

#if SERVEZA_WEB_USE_SSL
class https_operation final : public std::enable_shared_from_this<https_operation> {
public:
  https_operation(const details::application_impl& application, settings config,
                  serveza::session_context<net::ip::tcp>& ctx, net::ssl::context& ssl_ctx, completion_handler handler)
    : m_application{application}
    , m_config{std::move(config)}
    , m_ctx{ctx}
    , m_stream{ctx.socket(), ssl_ctx}
    , m_deadline{ctx.socket()}
    , m_handler{std::move(handler)}
  {}

  void start()
  {
    m_deadline.arm(m_config.tls_handshake_timeout);
    m_stream.async_handshake(net::ssl::stream_base::server, [self = shared_from_this()](sys::error_code handshakeEc) {
      self->handshaken(handshakeEc);
    });
  }

private:
  void handshaken(sys::error_code handshakeEc)
  {
    m_deadline.cancel();
    if (handshakeEc) return finish(expected_disconnect(handshakeEc) ? sys::error_code{} : handshakeEc);
    m_loop = std::make_shared<request_loop<decltype(m_stream)>>(m_application, m_config, m_stream, m_ctx.socket(),
                                                                m_ctx.listener_id(), &m_ctx.info());
    m_loop->start([self = shared_from_this()](sys::error_code requestEc) { self->requests_completed(requestEc); });
  }

  void requests_completed(sys::error_code requestEc)
  {
    const bool upgraded = m_loop->upgraded();
    m_loop.reset();
    if (requestEc || upgraded || !m_ctx.socket().is_open()) return finish(requestEc);
    m_deadline.arm(m_config.tls_shutdown_timeout);
    m_stream.async_shutdown([self = shared_from_this()](sys::error_code shutdownEc) {
      self->m_deadline.cancel();
      if (shutdownEc == net::error::eof || shutdownEc == net::ssl::error::stream_truncated ||
          shutdownEc == net::error::operation_aborted)
        shutdownEc.clear();
      self->finish(shutdownEc);
    });
  }

  void finish(sys::error_code ec)
  {
    complete(m_ctx.get_executor(), std::move(m_handler), ec);
  }

  const details::application_impl& m_application;
  settings m_config;
  serveza::session_context<net::ip::tcp>& m_ctx;
  net::ssl::stream<net::ip::tcp::socket&> m_stream;
  socket_deadline m_deadline;
  completion_handler m_handler;
  std::shared_ptr<request_loop<decltype(m_stream)>> m_loop;
};
#endif

} // namespace

http_session::http_session(application app)
  : m_application{std::move(app)}
{
  if (!m_application.m_impl) throw std::invalid_argument{"http_session requires a built application"};
  m_settings = m_application.m_impl->config();
}

void http_session::do_async_run(serveza::session_context<protocol_type>& ctx, completion_handler handler)
{
  auto operation = std::make_shared<request_loop<net::ip::tcp::socket>>(*m_application.m_impl, m_settings, ctx.socket(),
                                                                        ctx.socket(), ctx.listener_id(), &ctx.info());
  operation->start(
      [operation = std::move(operation), handler = std::move(handler)](sys::error_code ec) mutable { handler(ec); });
}

#if SERVEZA_WEB_USE_SSL
https_session::https_session(application app, net::ssl::context& ssl_ctx)
  : m_application{std::move(app)}
  , m_ssl_ctx{ssl_ctx}
{
  if (!m_application.m_impl) throw std::invalid_argument{"https_session requires a built application"};
  m_settings = m_application.m_impl->config();
}

void https_session::do_async_run(serveza::session_context<protocol_type>& ctx, completion_handler handler)
{
  std::make_shared<https_operation>(*m_application.m_impl, m_settings, ctx, m_ssl_ctx, std::move(handler))->start();
}
#endif

} // namespace serveza::web
