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

#ifndef SERVEZA_WEB_MIDDLEWARE_SECURITY_HEADERS_H
#define SERVEZA_WEB_MIDDLEWARE_SECURITY_HEADERS_H

#include <string>

#include <serveza/web/async.h>
#include <serveza/web/export.h>

namespace serveza::web {
class continuation;
class request_context;
} // namespace serveza::web

namespace serveza::web::middleware {

/** @brief Response security header values applied by @ref security_headers. */
struct security_headers_options {
  /** @brief Content-Security-Policy value; empty omits the field. */
  std::string content_security_policy;

  /** @brief Referrer-Policy value; empty omits the field. */
  std::string referrer_policy{"no-referrer"};

  /** @brief Permissions-Policy value; empty omits the field. */
  std::string permissions_policy;

  /** @brief Strict-Transport-Security value; empty omits the field. */
  std::string strict_transport_security;

  /** @brief Whether to emit @c X-Frame-Options: @c DENY. */
  bool deny_framing{true};

  /** @brief Whether to emit @c X-Content-Type-Options: @c nosniff. */
  bool prevent_content_type_sniffing{true};
};

/** @brief Middleware that adds configured browser security headers before continuing. */
class SERVEZA_WEB_API security_headers final {
public:
  /** @brief Creates security header middleware using @p options. */
  explicit security_headers(security_headers_options options = {});

  /** @brief Adds configured response fields and continues the middleware chain. */
  void operator()(request_context& ctx, continuation& next, completion_handler handler) const;

private:
  security_headers_options m_options;
};

} // namespace serveza::web::middleware

#endif // SERVEZA_WEB_MIDDLEWARE_SECURITY_HEADERS_H
