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

#ifndef SERVEZA_WEB_CONTEXT_H
#define SERVEZA_WEB_CONTEXT_H

#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <boost/asio/any_completion_handler.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/beast/http.hpp>

#include <serveza/connection_info.h>

#include <serveza/web/async.h>
#include <serveza/web/body.h>
#include <serveza/web/export.h>
#include <serveza/web/route_params.h>
#include <serveza/web/storage.h>
#include <serveza/web/target.h>
#include <serveza/web/websocket.h>

namespace serveza::web {

namespace details {
struct request_state;
class dispatcher;
struct traffic_observer_access;
} // namespace details

/**
 * @brief Non-owning asynchronous interface to one HTTP request and response.
 *
 * The context and all views obtained from it remain valid until the current
 * route handler completes. Exactly one response or WebSocket upgrade may be
 * committed. Every `async_*` operation preserves its token's associated
 * executor, allocator and cancellation slot.
 */
class SERVEZA_WEB_API request_context final {
public:
  /** @brief HTTP header container used for request and response fields. */
  using fields_type = boost::beast::http::fields;

  request_context(const request_context&) = delete;
  request_context(request_context&&) = delete;
  request_context& operator=(const request_context&) = delete;
  request_context& operator=(request_context&&) = delete;

  /** @brief Returns the request method. */
  [[nodiscard]] boost::beast::http::verb method() const noexcept;
  /** @brief Returns the HTTP version encoded as 10 or 11. */
  [[nodiscard]] unsigned version() const noexcept;
  /** @brief Returns whether the request asks to keep the connection alive. */
  [[nodiscard]] bool keep_alive() const noexcept;
  /** @brief Returns the parsed request target. */
  [[nodiscard]] target_view target() const noexcept;
  /** @brief Returns the currently matched route pattern. */
  [[nodiscard]] std::string_view matched_route() const noexcept;
  /** @brief Returns the accepting listener identifier. */
  [[nodiscard]] std::uint64_t listener_id() const noexcept;
  /** @brief Returns connection metadata, or @c nullptr when unavailable. */
  [[nodiscard]] const serveza::connection_info* connection() const noexcept;
  /** @brief Returns the immutable request headers. */
  [[nodiscard]] const fields_type& request_headers() const noexcept;
  /** @brief Returns all parameters captured by the current route. */
  [[nodiscard]] const web::route_params& params() const noexcept;
  /** @brief Returns route parameter @p name when present. */
  [[nodiscard]] std::optional<std::string_view> param(std::string_view name) const noexcept;
  /** @brief Returns mutable request-local typed storage. */
  [[nodiscard]] web::storage& storage() noexcept;
  /** @brief Returns immutable request-local typed storage. */
  [[nodiscard]] const web::storage& storage() const noexcept;
  /** @brief Returns mutable response headers until the response is committed. */
  [[nodiscard]] fields_type& response_headers() noexcept;
  /** @brief Returns the current response headers. */
  [[nodiscard]] const fields_type& response_headers() const noexcept;
  /** @brief Returns the connection executor. */
  [[nodiscard]] boost::asio::any_io_executor get_executor() const;
  /** @brief Returns the declared request-body length when known. */
  [[nodiscard]] std::optional<std::uint64_t> content_length() const noexcept;
  /** @brief Returns the active request-body limit in bytes. */
  [[nodiscard]] std::size_t maximum_body_size() const noexcept;
  /** @brief Narrows the request-body limit to @p limit bytes before reading. */
  void set_body_limit(std::size_t limit);
  /** @brief Returns whether the request body has been consumed or discarded. */
  [[nodiscard]] bool body_consumed() const noexcept;
  /** @brief Returns the number of request-body bytes consumed so far. */
  [[nodiscard]] std::size_t request_body_size() const noexcept;

  /** Completion signature: `void(error_code, string_view)`. */
  template<typename CompletionToken>
  auto async_read_body(CompletionToken&& token)
  {
    validate_read_body();
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code, std::string_view)>(
        [this](auto handler) mutable { do_async_read_body(body_handler{std::move(handler)}); }, token);
  }

  /** Completion signature: `void(error_code, byte_view)`. */
  template<typename CompletionToken>
  auto async_read_body_bytes(CompletionToken&& token)
  {
    validate_read_body();
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code, byte_view)>(
        [this](auto handler) mutable { do_async_read_body_bytes(byte_body_handler{std::move(handler)}); }, token);
  }

  /** Streams body chunks to a synchronous consumer. Completion signature: `void(error_code)`. */
  template<typename Consumer, typename CompletionToken>
  auto async_read_body_chunks(Consumer&& consumer, CompletionToken&& token)
  {
    using consumer_type = std::decay_t<Consumer>;
    static_assert(std::is_invocable_v<consumer_type&, std::string_view>,
                  "body consumer must be callable with (string_view)");
    validate_read_body_chunks();
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, consumer = consumer_type{std::forward<Consumer>(consumer)}](auto handler) mutable {
          auto state = std::make_shared<consumer_type>(std::move(consumer));
          auto* state_value = state.get();
          do_async_read_body_chunks(
              state_value, [](void* opaque, std::string_view chunk) { (*static_cast<consumer_type*>(opaque))(chunk); },
              completion_handler{details::retain_completion(get_executor(), std::move(handler), std::move(state))});
        },
        token);
  }

  /** Streams body chunks as bytes. Completion signature: `void(error_code)`. */
  template<typename Consumer, typename CompletionToken>
  auto async_read_body_byte_chunks(Consumer&& consumer, CompletionToken&& token)
  {
    using consumer_type = std::decay_t<Consumer>;
    static_assert(std::is_invocable_v<consumer_type&, byte_view>, "body consumer must be callable with (byte_view)");
    return async_read_body_chunks([consumer = consumer_type{std::forward<Consumer>(consumer)}](
                                      std::string_view chunk) mutable { consumer(byte_view{chunk}); },
                                  std::forward<CompletionToken>(token));
  }

  /** Completion signature: `void(error_code)`. */
  template<typename CompletionToken>
  auto async_discard_body(CompletionToken&& token)
  {
    validate_discard_body();
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this](auto handler) mutable { do_async_discard_body(completion_handler{std::move(handler)}); }, token);
  }

  /** @brief Returns whether a response or WebSocket upgrade has been committed. */
  [[nodiscard]] bool response_committed() const noexcept;
  /** @brief Returns the committed response status when available. */
  [[nodiscard]] std::optional<boost::beast::http::status> response_status() const noexcept;
  /** @brief Returns the number of response-body bytes written so far. */
  [[nodiscard]] std::size_t response_body_size() const noexcept;
  /** @brief Disables keep-alive after the current response. */
  void close_after_response() noexcept;

  /** Sends a buffered text response. Completion signature: `void(error_code)`. */
  template<typename CompletionToken>
  auto async_send(boost::beast::http::status status, std::string body, std::string_view content_type,
                  CompletionToken&& token)
  {
    validate_send();
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, status, body = std::move(body), content_type = std::string{content_type}](auto handler) mutable {
          do_async_send(status, std::move(body), content_type, completion_handler{std::move(handler)});
        },
        token);
  }

  template<typename CompletionToken>
  /** @brief Sends text with the default `text/plain; charset=utf-8` content type. */
  auto async_send(boost::beast::http::status status, std::string body, CompletionToken&& token)
  {
    validate_send();
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, status, body = std::move(body)](auto handler) mutable {
          do_async_send(status, std::move(body), "text/plain; charset=utf-8", completion_handler{std::move(handler)});
        },
        token);
  }

  /** Sends a buffered byte response. Completion signature: `void(error_code)`. */
  template<typename CompletionToken>
  auto async_send(boost::beast::http::status status, byte_buffer body, std::string_view content_type,
                  CompletionToken&& token)
  {
    validate_send();
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, status, body = std::move(body), content_type = std::string{content_type}](auto handler) mutable {
          do_async_send(status, std::move(body), content_type, completion_handler{std::move(handler)});
        },
        token);
  }

  template<typename CompletionToken>
  /** @brief Sends bytes with the default `application/octet-stream` content type. */
  auto async_send(boost::beast::http::status status, byte_buffer body, CompletionToken&& token)
  {
    validate_send();
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, status, body = std::move(body)](auto handler) mutable {
          do_async_send(status, std::move(body), "application/octet-stream", completion_handler{std::move(handler)});
        },
        token);
  }

  /** Sends a chunked response. Producer completion signature: `void(error_code)`. */
  template<typename Producer, typename CompletionToken>
  auto async_send_chunked(boost::beast::http::status status, std::string_view content_type, Producer&& producer,
                          CompletionToken&& token)
  {
    using producer_type = std::decay_t<Producer>;
    static_assert(std::is_invocable_r_v<void, producer_type&, response_writer&, completion_handler>,
                  "chunk producer must accept (response_writer&, completion_handler); wrap stackful and C++20 "
                  "producers with yield_chunk_producer or awaitable_chunk_producer");
    validate_send_chunked(status);
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, status, content_type = std::string{content_type},
         producer = producer_type{std::forward<Producer>(producer)}](auto handler) mutable {
          auto state = std::make_shared<producer_type>(std::move(producer));
          auto* state_value = state.get();
          do_async_send_chunked(
              status, std::move(content_type), state_value,
              [](void* value, response_writer& writer, completion_handler done) {
                (*static_cast<producer_type*>(value))(writer, std::move(done));
              },
              completion_handler{details::retain_completion(get_executor(), std::move(handler), std::move(state))});
        },
        token);
  }

  template<typename CompletionToken>
  /** @brief Sends a response status with an empty body. */
  auto async_send_status(boost::beast::http::status status, CompletionToken&& token)
  {
    return async_send(status, std::string{}, std::string_view{}, std::forward<CompletionToken>(token));
  }

  /** Completion signature: `void(error_code, bool)`, where bool reports whether the file was opened. */
  template<typename CompletionToken>
  auto async_send_file(boost::beast::http::status status, std::filesystem::path path, std::string_view content_type,
                       CompletionToken&& token)
  {
    validate_send();
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code, bool)>(
        [this, status, path = std::move(path), content_type = std::string{content_type}](auto handler) mutable {
          do_async_send_file(status, std::move(path), std::move(content_type), bool_handler{std::move(handler)});
        },
        token);
  }

  /** Completion signature: `void(error_code, bool)`, where bool reports whether the range was opened. */
  template<typename CompletionToken>
  auto async_send_file_range(boost::beast::http::status status, std::filesystem::path path, std::uint64_t offset,
                             std::uint64_t length, std::string_view content_type, CompletionToken&& token)
  {
    validate_send();
    validate_file_range(length);
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code, bool)>(
        [this, status, path = std::move(path), offset, length,
         content_type = std::string{content_type}](auto handler) mutable {
          do_async_send_file_range(status, std::move(path), offset, length, std::move(content_type),
                                   bool_handler{std::move(handler)});
        },
        token);
  }

  /** @brief Returns whether the request contains a valid WebSocket upgrade. */
  [[nodiscard]] bool websocket_upgrade_requested() const noexcept;

  /** Runs a callback WebSocket endpoint. Completion signature: `void(error_code)`. */
  template<typename Endpoint, typename CompletionToken>
  auto async_accept_websocket(Endpoint&& endpoint, websocket_options options, CompletionToken&& token)
  {
    using endpoint_type = std::decay_t<Endpoint>;
    static_assert(std::is_invocable_r_v<void, endpoint_type&, websocket_connection&, completion_handler>,
                  "WebSocket endpoint must accept (websocket_connection&, completion_handler); wrap stackful and "
                  "C++20 endpoints with yield_websocket_endpoint or awaitable_websocket_endpoint");
    validate_websocket_accept(options);
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, endpoint = endpoint_type{std::forward<Endpoint>(endpoint)},
         options = std::move(options)](auto handler) mutable {
          auto state = std::make_shared<endpoint_type>(std::move(endpoint));
          auto* state_value = state.get();
          do_async_accept_websocket(
              state_value,
              [](void* value, websocket_connection& connection, completion_handler done) {
                (*static_cast<endpoint_type*>(value))(connection, std::move(done));
              },
              std::move(options),
              completion_handler{details::retain_completion(get_executor(), std::move(handler), std::move(state))});
        },
        token);
  }

  /// @cond INTERNAL
public:
  friend class details::dispatcher;
  friend struct details::request_state;
  friend struct details::traffic_observer_access;

  using body_handler = boost::asio::any_completion_handler<void(boost::system::error_code, std::string_view)>;
  using byte_body_handler = boost::asio::any_completion_handler<void(boost::system::error_code, byte_view)>;
  using bool_handler = boost::asio::any_completion_handler<void(boost::system::error_code, bool)>;
  using websocket_callback = void (*)(void*, websocket_connection&, completion_handler);
  using body_chunk_callback = void (*)(void*, std::string_view);
  using chunked_response_callback = void (*)(void*, response_writer&, completion_handler);

  explicit request_context(details::request_state& state) noexcept;

  void do_async_read_body(body_handler handler);
  void do_async_read_body_bytes(byte_body_handler handler);
  void do_async_read_body_chunks(void* consumer, body_chunk_callback callback, completion_handler handler);
  void do_async_discard_body(completion_handler handler);
  void do_async_send(boost::beast::http::status status, std::string body, std::string_view content_type,
                     completion_handler handler);
  void do_async_send(boost::beast::http::status status, byte_buffer body, std::string_view content_type,
                     completion_handler handler);
  void do_async_send_chunked(boost::beast::http::status status, std::string content_type, void* producer,
                             chunked_response_callback callback, completion_handler handler);
  void do_async_send_file(boost::beast::http::status status, std::filesystem::path path, std::string content_type,
                          bool_handler handler);
  void do_async_send_file_range(boost::beast::http::status status, std::filesystem::path path, std::uint64_t offset,
                                std::uint64_t length, std::string content_type, bool_handler handler);
  void do_async_accept_websocket(void* endpoint, websocket_callback callback, websocket_options options,
                                 completion_handler handler);
  [[nodiscard]] std::exception_ptr take_exception() noexcept;
  void report_exception(std::exception_ptr ep) noexcept;

  /// @endcond

private:
  void validate_read_body() const;
  void validate_read_body_chunks() const;
  void validate_discard_body() const;
  void validate_send() const;
  void validate_send_chunked(boost::beast::http::status status) const;
  static void validate_file_range(std::uint64_t length);
  void validate_websocket_accept(const websocket_options& options) const;
  details::request_state* m_state;
};

} // namespace serveza::web

#endif // SERVEZA_WEB_CONTEXT_H
