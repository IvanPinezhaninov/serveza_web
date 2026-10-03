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

#ifndef SERVEZA_WEB_MIDDLEWARE_REQUEST_ID_H
#define SERVEZA_WEB_MIDDLEWARE_REQUEST_ID_H

#include <cstddef>
#include <string>

#include <serveza/web/async.h>
#include <serveza/web/export.h>

namespace serveza::web {
class continuation;
class request_context;
} // namespace serveza::web

namespace serveza::web::middleware {

/** @brief Request-local identifier installed in @ref storage by @ref request_id. */
struct SERVEZA_WEB_API request_id_value {
  /** @brief Validated incoming or generated identifier. */
  std::string value;
};

/** @brief Request identifier header and trust policy. */
struct request_id_options {
  /** @brief Request and response header carrying the identifier. */
  std::string header{"X-Request-ID"};

  /** @brief Whether a valid incoming header value may be reused. */
  bool trust_incoming{};

  /** @brief Maximum accepted length of a trusted incoming identifier. */
  std::size_t max_incoming_length{128};
};

/** @brief Middleware that validates an incoming ID or generates a random UUIDv4. */
class SERVEZA_WEB_API request_id final {
public:
  /** @brief Creates request ID middleware using @p options. */
  explicit request_id(request_id_options options = {});

  /** @brief Installs a @ref request_id_value, sets the response header, and continues. */
  void operator()(request_context& ctx, continuation& next, completion_handler handler) const;

private:
  request_id_options m_options;
};

} // namespace serveza::web::middleware

#endif // SERVEZA_WEB_MIDDLEWARE_REQUEST_ID_H
