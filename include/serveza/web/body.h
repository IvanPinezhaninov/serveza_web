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

#ifndef SERVEZA_WEB_BODY_H
#define SERVEZA_WEB_BODY_H

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>

#include <serveza/web/async.h>
#include <serveza/web/export.h>

namespace serveza::web {

namespace details {
struct request_state;
} // namespace details

class request_context;

/** @brief Owning container for arbitrary HTTP body bytes. */
using byte_buffer = std::vector<std::byte>;

/**
 * @brief Lightweight non-owning view of arbitrary bytes.
 *
 * The view does not extend the lifetime of the referenced storage.
 */
class SERVEZA_WEB_API byte_view final {
public:
  /** @brief Creates an empty byte view. */
  byte_view() noexcept;

  /** @brief Creates a view over @p size bytes beginning at @p data. */
  byte_view(const void* data, std::size_t size) noexcept;

  /** @brief Creates a view over an owning byte buffer. */
  byte_view(const byte_buffer& value) noexcept;

  /** @brief Creates a byte view over the bytes of a string view. */
  byte_view(std::string_view value) noexcept;

  /** @brief Returns the first byte, or @c nullptr for an empty view. */
  [[nodiscard]] const std::byte* data() const noexcept;

  /** @brief Returns the number of bytes in the view. */
  [[nodiscard]] std::size_t size() const noexcept;

  /** @brief Returns whether the view is empty. */
  [[nodiscard]] bool empty() const noexcept;

  /** @brief Returns an iterator to the first byte. */
  [[nodiscard]] const std::byte* begin() const noexcept;

  /** @brief Returns an iterator one past the last byte. */
  [[nodiscard]] const std::byte* end() const noexcept;

private:
  const std::byte* m_data{};
  std::size_t m_size{};
};

/**
 * @brief Asynchronous writer for a response using HTTP chunked transfer coding.
 *
 * A writer is supplied to a @ref request_context::async_send_chunked producer. It is
 * valid only while that producer is running and does not retain chunk storage.
 */
class SERVEZA_WEB_API response_writer final {
public:
  /** @brief Response writers cannot be copied. */
  response_writer(const response_writer&) = delete;

  /** @brief Response writers cannot be moved. */
  response_writer(response_writer&&) = delete;

  /** @brief Response writers cannot be copy-assigned. */
  response_writer& operator=(const response_writer&) = delete;

  /** @brief Response writers cannot be move-assigned. */
  response_writer& operator=(response_writer&&) = delete;

  /**
   * @brief Writes one arbitrary byte chunk. Empty chunks are ignored.
   *
   * The completion signature is `void(boost::system::error_code)`. The bytes
   * referenced by @p value must remain valid until completion.
   */
  template<typename CompletionToken>
  auto async_write(byte_view value, CompletionToken&& token)
  {
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, value](auto handler) mutable { do_async_write(value, completion_handler{std::move(handler)}); }, token);
  }

  /** @brief Writes one text chunk without copying it into an intermediate body. */
  template<typename CompletionToken>
  auto async_write(std::string_view value, CompletionToken&& token)
  {
    return async_write(byte_view{value}, std::forward<CompletionToken>(token));
  }

  /** @brief Returns the number of body bytes written so far. */
  [[nodiscard]] std::size_t bytes_written() const noexcept;

  /** @brief Returns the connection executor. */
  [[nodiscard]] boost::asio::any_io_executor get_executor() const;

private:
  friend class request_context;

  /// @cond INTERNAL
public:
  explicit response_writer(details::request_state& state) noexcept;

  void do_async_write(byte_view value, completion_handler handler);
  /// @endcond

private:
  details::request_state* m_state;
};

} // namespace serveza::web

#endif // SERVEZA_WEB_BODY_H
