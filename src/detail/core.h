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

#ifndef SERVEZA_WEB_DETAIL_CORE_H
#define SERVEZA_WEB_DETAIL_CORE_H

#include <chrono>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/any_completion_executor.hpp>
#include <boost/asio/any_completion_handler.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/beast/core/file.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/container/small_vector.hpp>

#include <serveza/connection_info.h>

#include <serveza/web/context.h>
#include <serveza/web/detail/layer.h>
#include <serveza/web/middleware/traffic_observer.h>
#include <serveza/web/settings.h>
#include <serveza/web/websocket.h>

#include "detail/completion_callback.h"
#include "detail/route_pattern.h"

namespace serveza::web::details {

using parser_type = boost::beast::http::request_parser<boost::beast::http::buffer_body>;
using string_response = boost::beast::http::response<boost::beast::http::string_body>;
using byte_response = boost::beast::http::response<boost::beast::http::vector_body<std::byte>>;
using empty_response = boost::beast::http::response<boost::beast::http::empty_body>;
using file_response = boost::beast::http::response<boost::beast::http::file_body>;

class transport {
public:
  using websocket_callback = void (*)(void*, websocket_connection&, completion_handler);

  transport() = default;

  transport(const transport&) = delete;

  transport& operator=(const transport&) = delete;

  virtual ~transport() = default;

  [[nodiscard]] virtual boost::asio::any_io_executor get_executor() = 0;
  [[nodiscard]] virtual const boost::asio::any_completion_executor& get_completion_executor() const noexcept = 0;

  virtual void async_read_header(boost::beast::flat_buffer& buffer, parser_type& parser,
                                 completion_callback handler) = 0;

  virtual void async_read_some(boost::beast::flat_buffer& buffer, parser_type& parser, completion_callback handler) = 0;

  virtual void async_write(string_response& res, bool skip_body, completion_callback handler) = 0;

  virtual void async_write(byte_response& res, bool skip_body, completion_callback handler) = 0;

  virtual void async_write(file_response& res, bool skip_body, completion_callback handler) = 0;

  virtual void async_write_file_range(empty_response& res, boost::beast::file& file, std::uint64_t length,
                                      bool skip_body, completion_callback handler) = 0;

  virtual void async_write_chunked_header(empty_response& res, bool skip_body, completion_callback handler) = 0;

  virtual void async_write_chunk(const void* data, std::size_t size, completion_callback handler) = 0;

  virtual void async_write_chunk_last(completion_callback handler) = 0;

  virtual void async_accept_websocket(const parser_type& parser, const boost::beast::http::fields& response_fields,
                                      const websocket_options& options, void* endpoint, websocket_callback callback,
                                      completion_callback handler) = 0;
};

class websocket_io {
public:
  websocket_io() = default;

  websocket_io(const websocket_io&) = delete;

  websocket_io& operator=(const websocket_io&) = delete;

  virtual ~websocket_io() = default;

  [[nodiscard]] virtual bool is_open() const noexcept = 0;

  using read_handler =
      boost::asio::any_completion_handler<void(boost::system::error_code, std::optional<websocket_message>)>;

  [[nodiscard]] virtual boost::asio::any_io_executor get_executor() = 0;

  virtual void async_read(read_handler handler) = 0;

  [[nodiscard]] virtual websocket_send_result try_send_text(std::string value) = 0;

  [[nodiscard]] virtual websocket_send_result try_send_binary(byte_buffer value) = 0;

  virtual void async_write_text(std::string value, completion_handler handler) = 0;

  virtual void async_write_binary(byte_buffer value, completion_handler handler) = 0;

  virtual void async_ping(std::string value, completion_handler handler) = 0;

  virtual void async_close(std::uint16_t code, std::string reason, completion_handler handler) = 0;
};

struct websocket_connection_access final {
  static std::shared_ptr<websocket_connection> make(std::shared_ptr<websocket_io> io)
  {
    return std::shared_ptr<websocket_connection>{new websocket_connection{std::move(io)}};
  }
};

enum class request_body_access {
  untouched,
  buffered,
  streamed,
};

enum class response_progress {
  idle,
  started,
  completed,
};

struct request_state final {
  request_state(transport& io_value, boost::beast::flat_buffer& buffer_value, parser_type& parser_value,
                target_view target_value, const settings& settings_value, std::uint64_t listener_id_value,
                const serveza::connection_info* connection_value)
    : io{io_value}
    , buffer{buffer_value}
    , parser{parser_value}
    , parsed_target{target_value}
    , config{settings_value}
    , listener_id{listener_id_value}
    , connection{connection_value}
  {}

  request_context make_context() noexcept
  {
    return request_context{*this};
  }

  void start_response(boost::beast::http::status status, std::size_t body_size) noexcept
  {
    response_status = status;
    response_body_size = body_size;
    response_route = current_route;
    response = response_progress::started;
  }

  void complete_response() noexcept
  {
    response = response_progress::completed;
  }

  transport& io;
  boost::beast::flat_buffer& buffer;
  parser_type& parser;
  target_view parsed_target;
  const settings& config;
  std::uint64_t listener_id{};
  const serveza::connection_info* connection{};
  std::string_view current_route;
  std::string_view response_route;
  web::route_params route_values;
  web::storage request_storage;
  boost::beast::http::fields response_fields;
  std::optional<string_response> string_response_message;
  std::optional<byte_response> byte_response_message;
  std::optional<completion_handler> response_completion;
  std::string body;
  request_body_access body_access{};
  std::size_t request_body_size{};
  response_progress response{response_progress::idle};
  bool websocket_upgraded{};
  std::optional<boost::beast::http::status> response_status;
  std::size_t response_body_size{};
  bool force_close{};
  std::exception_ptr exception;
  struct traffic_observer_binding final {
    void* sink{};
    middleware::details::traffic_observer_callbacks callbacks;
    std::chrono::steady_clock::time_point started;
  };
  boost::container::small_vector<traffic_observer_binding, 2> traffic_observers;
};

struct traffic_observer_access final {
  static request_state& state(request_context& ctx) noexcept
  {
    return *ctx.m_state;
  }
};

void observe_request_body(request_state& state, byte_view chunk) noexcept;
void observe_response_headers(request_state& state, boost::beast::http::status status,
                              const boost::beast::http::fields& headers, middleware::response_body_kind body_kind,
                              std::optional<std::uint64_t> body_size) noexcept;
void observe_response_body(request_state& state, byte_view chunk) noexcept;
void observe_response_file(request_state& state, const std::filesystem::path& path, std::uint64_t offset,
                           std::uint64_t size) noexcept;
SERVEZA_WEB_API void complete_traffic_observers(request_state& state, std::exception_ptr ep) noexcept;

struct compiled_layer final {
  std::optional<boost::beast::http::verb> method;
  std::optional<compiled_pattern> pattern;
  std::shared_ptr<layer> callback;
};

struct dispatch_node final {
  std::vector<std::size_t> candidates;
  std::map<std::string, dispatch_node, std::less<>> literals;
  std::unique_ptr<dispatch_node> variable;
};

struct dispatch_index final {
  std::optional<boost::beast::http::verb> method;
  dispatch_node root;
};

class dispatcher;

class SERVEZA_WEB_API application_impl final {
public:
  application_impl(settings config, std::vector<compiled_layer> layers);
  application_impl(const application_impl&) = delete;
  application_impl& operator=(const application_impl&) = delete;

  [[nodiscard]] const settings& config() const noexcept
  {
    return m_settings;
  }

  [[nodiscard]] const std::vector<compiled_layer>& layers() const noexcept
  {
    return m_layers;
  }

  [[nodiscard]] const std::vector<dispatch_index>& indexes() const noexcept
  {
    return m_indexes;
  }

  template<typename CompletionToken>
  auto async_dispatch(request_state& state, CompletionToken&& token) const
  {
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, &state](auto handler) mutable {
          dispatch(state,
                   completion_callback{[&state, handler = std::move(handler)](boost::system::error_code ec) mutable {
                     dispatch_completion(state.io.get_executor(), std::move(handler), ec);
                   }});
        },
        token);
  }

  void dispatch(request_state& state, completion_callback handler) const;
  void dispatch(request_state& state, completion_callback handler,
                std::shared_ptr<dispatcher>& reusable_dispatcher) const;

private:
  settings m_settings;
  std::vector<compiled_layer> m_layers;
  std::vector<dispatch_index> m_indexes;
};

SERVEZA_WEB_API target_view parse_target(std::string_view raw);

} // namespace serveza::web::details

#endif // SERVEZA_WEB_DETAIL_CORE_H
