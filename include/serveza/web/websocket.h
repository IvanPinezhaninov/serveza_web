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

#ifndef SERVEZA_WEB_WEBSOCKET_H
#define SERVEZA_WEB_WEBSOCKET_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/any_completion_handler.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>

#include <serveza/web/async.h>
#include <serveza/web/body.h>
#include <serveza/web/export.h>

namespace serveza::web {

namespace details {
class websocket_io;
struct websocket_connection_access;
} // namespace details

/** @brief Limits and timeout behavior applied to an accepted WebSocket connection. */
struct websocket_options {
  /** @brief Maximum complete incoming message size in bytes. */
  std::size_t max_message_size{1024 * 1024};

  /** @brief Maximum time allowed to complete the WebSocket handshake. */
  std::chrono::seconds handshake_timeout{10};

  /** @brief Maximum idle interval before the connection times out. */
  std::chrono::seconds idle_timeout{60};

  /** @brief Whether the underlying WebSocket stream sends idle keep-alive pings. */
  bool keep_alive_pings{true};

  /** @brief Maximum time allowed to drain writes and complete a closing handshake. */
  std::chrono::seconds close_timeout{5};

  /** @brief Maximum accepted messages not yet completely written, including the active write. */
  std::size_t max_pending_messages{64};

  /** @brief Maximum total payload bytes not yet completely written. */
  std::size_t max_pending_bytes{4 * 1024 * 1024};
};

/// @cond INTERNAL
namespace details {

SERVEZA_WEB_API void validate_websocket_options(const websocket_options& options);

} // namespace details
/// @endcond

/** @brief Identifies the payload kind of a complete WebSocket message. */
enum class websocket_message_type {
  /** @brief A UTF-8 text message. */
  text,

  /** @brief An arbitrary binary message. */
  binary,
};

/** @brief One complete WebSocket text or binary message. */
struct SERVEZA_WEB_API websocket_message {
  /** @brief Message payload stored without a text-only representation. */
  byte_buffer data;

  /** @brief Message payload kind. */
  websocket_message_type type{websocket_message_type::binary};

  /** @brief Returns whether this is a text message. */
  [[nodiscard]] bool is_text() const noexcept;

  /**
   * @brief Returns a non-owning text view of the payload bytes.
   * @throws std::logic_error if this is a binary message.
   */
  [[nodiscard]] std::string_view text() const;

  /** @brief Returns a non-owning view of the payload bytes. */
  [[nodiscard]] byte_view bytes() const noexcept;
};

/** @brief Result of attempting to add a message to a WebSocket send queue. */
enum class websocket_send_result {
  /** @brief The message was accepted and will be written in FIFO order. */
  queued,

  /** @brief The connection no longer accepts outgoing messages. */
  closed,

  /** @brief The configured outgoing queue limit would be exceeded. */
  queue_full,
};

/**
 * @brief Copyable thread-safe handle for sending messages to a WebSocket.
 *
 * Messages are moved into a bounded per-connection FIFO and all Beast writes
 * are serialized on the connection executor. The handle may be retained by an
 * application event hub while the endpoint waits for an incoming message. It
 * becomes closed when the endpoint finishes.
 */
class SERVEZA_WEB_API websocket_sender final {
public:
  /** @brief Creates a sender not associated with a connection. */
  websocket_sender() noexcept;

  /** @brief Returns whether the associated endpoint still accepts messages. */
  [[nodiscard]] bool is_open() const noexcept;

  /**
   * @brief Attempts to enqueue an owning text message without blocking.
   * @return Queue admission result; a rejected payload is destroyed by this call.
   */
  [[nodiscard]] websocket_send_result try_send_text(std::string value) const;

  /**
   * @brief Attempts to enqueue an owning binary message without blocking.
   * @return Queue admission result; a rejected payload is destroyed by this call.
   */
  [[nodiscard]] websocket_send_result try_send_binary(byte_buffer value) const;

private:
  friend class websocket_connection;

  explicit websocket_sender(std::weak_ptr<details::websocket_io> io) noexcept;

  std::weak_ptr<details::websocket_io> m_io;
};

/**
 * @brief Non-owning asynchronous interface to an accepted WebSocket.
 *
 * The connection is valid only during the endpoint invocation supplied to
 * @ref request_context::async_accept_websocket.
 */
class SERVEZA_WEB_API websocket_connection final {
public:
  /** @brief WebSocket connections cannot be copied. */
  websocket_connection(const websocket_connection&) = delete;

  /** @brief WebSocket connections cannot be moved. */
  websocket_connection(websocket_connection&&) = delete;

  /** @brief WebSocket connections cannot be copy-assigned. */
  websocket_connection& operator=(const websocket_connection&) = delete;

  /** @brief WebSocket connections cannot be move-assigned. */
  websocket_connection& operator=(websocket_connection&&) = delete;

  /** @brief Returns whether the WebSocket stream remains open. */
  [[nodiscard]] bool is_open() const noexcept;

  /** @brief Returns a copyable sender suitable for application event hubs. */
  [[nodiscard]] websocket_sender sender() const noexcept;

  /** @brief Returns the connection executor. */
  [[nodiscard]] boost::asio::any_io_executor get_executor() const;

  /** Completion signature: `void(error_code, optional<websocket_message>)`. */
  template<typename CompletionToken>
  auto async_read(CompletionToken&& token)
  {
    return boost::asio::async_initiate<CompletionToken,
                                       void(boost::system::error_code, std::optional<websocket_message>)>(
        [this](auto handler) mutable { do_async_read(read_handler{std::move(handler)}); }, token);
  }

  /**
   * @brief Writes @p value as one complete text message.
   * @throws std::length_error if the bounded send queue cannot admit the message.
   */
  template<typename CompletionToken>
  auto async_write_text(std::string value, CompletionToken&& token)
  {
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, value = std::move(value)](auto handler) mutable {
          do_async_write_text(std::move(value), completion_handler{std::move(handler)});
        },
        token);
  }

  /**
   * @brief Writes @p value as one complete binary message.
   * @throws std::length_error if the bounded send queue cannot admit the message.
   */
  template<typename CompletionToken>
  auto async_write_binary(byte_buffer value, CompletionToken&& token)
  {
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, value = std::move(value)](auto handler) mutable {
          do_async_write_binary(std::move(value), completion_handler{std::move(handler)});
        },
        token);
  }

  /** @brief Copies @p value and writes it as one complete binary message. */
  template<typename CompletionToken>
  auto async_write_binary(byte_view value, CompletionToken&& token)
  {
    return async_write_binary(byte_buffer{value.begin(), value.end()}, std::forward<CompletionToken>(token));
  }

  /** @brief Sends a WebSocket ping containing @p value. */
  template<typename CompletionToken>
  auto async_ping(std::string value, CompletionToken&& token)
  {
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, value = std::move(value)](auto handler) mutable {
          do_async_ping(std::move(value), completion_handler{std::move(handler)});
        },
        token);
  }

  /** @brief Starts a closing handshake with application @p code and @p reason. */
  template<typename CompletionToken>
  auto async_close(std::uint16_t code, std::string reason, CompletionToken&& token)
  {
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, code, reason = std::move(reason)](auto handler) mutable {
          do_async_close(code, std::move(reason), completion_handler{std::move(handler)});
        },
        token);
  }

  /** @brief Starts a normal closing handshake with code 1000. */
  template<typename CompletionToken>
  auto async_close(CompletionToken&& token)
  {
    return async_close(1000, {}, std::forward<CompletionToken>(token));
  }

private:
  friend struct details::websocket_connection_access;

  explicit websocket_connection(std::shared_ptr<details::websocket_io> io) noexcept;

  using read_handler =
      boost::asio::any_completion_handler<void(boost::system::error_code, std::optional<websocket_message>)>;
  void do_async_read(read_handler handler);
  void do_async_write_text(std::string value, completion_handler handler);
  void do_async_write_binary(byte_buffer value, completion_handler handler);
  void do_async_ping(std::string value, completion_handler handler);
  void do_async_close(std::uint16_t code, std::string reason, completion_handler handler);

  std::shared_ptr<details::websocket_io> m_io;
};

} // namespace serveza::web

#endif // SERVEZA_WEB_WEBSOCKET_H
