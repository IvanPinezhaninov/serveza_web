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

#ifndef SERVEZA_WEB_SETTINGS_H
#define SERVEZA_WEB_SETTINGS_H

#include <chrono>
#include <cstddef>

namespace serveza::web {

/** @brief Controls whether a terminal slash is significant during route matching. */
enum class trailing_slash_policy {
  /** @brief Treat paths with and without a terminal slash as different paths. */
  strict,

  /** @brief Ignore one terminal slash while matching routes. */
  ignore,
};

/** @brief Limits and timeouts shared by every request in an application. */
struct settings {
  /** @brief Maximum number of non-empty segments accepted in a request path. */
  static constexpr std::size_t maximum_path_segments = 64;

  /** @brief Positive maximum time allowed for an individual HTTP request operation. */
  std::chrono::seconds request_timeout{10};

  /** @brief Positive maximum idle time while waiting for the next keep-alive request. */
  std::chrono::seconds keep_alive_timeout{60};

  /** @brief Positive maximum time allowed for a TLS handshake. */
  std::chrono::seconds tls_handshake_timeout{10};

  /** @brief Positive maximum time allowed for graceful TLS shutdown. */
  std::chrono::seconds tls_shutdown_timeout{5};

  /** @brief Maximum encoded HTTP header size in bytes, in the range 1..UINT32_MAX. */
  std::size_t header_limit{16 * 1024};

  /** @brief Application-wide maximum request body size in bytes. */
  std::size_t body_limit{1024 * 1024};

  /** @brief Positive maximum segment size accepted by a regular-expression route constraint. */
  std::size_t regex_segment_limit{1024};

  /** @brief Policy used to compare terminal slashes in request paths. */
  trailing_slash_policy trailing_slash{trailing_slash_policy::ignore};
};

} // namespace serveza::web

#endif // SERVEZA_WEB_SETTINGS_H
