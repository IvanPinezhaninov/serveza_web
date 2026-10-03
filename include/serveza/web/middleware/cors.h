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

#ifndef SERVEZA_WEB_MIDDLEWARE_CORS_H
#define SERVEZA_WEB_MIDDLEWARE_CORS_H

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include <serveza/web/async.h>
#include <serveza/web/export.h>

namespace serveza::web {
class continuation;
class request_context;
} // namespace serveza::web

namespace serveza::web::middleware {

/** @brief Cross-Origin Resource Sharing policy applied by @ref cors. */
struct cors_options {
  /** @brief Exact origins allowed when @ref allow_any_origin is disabled. */
  std::vector<std::string> allowed_origins;

  /** @brief Value returned in Access-Control-Allow-Methods. */
  std::string allowed_methods{"GET, HEAD, POST, PUT, PATCH, DELETE, OPTIONS"};

  /** @brief Value returned in Access-Control-Allow-Headers when non-empty. */
  std::string allowed_headers;

  /** @brief Value returned in Access-Control-Expose-Headers when non-empty. */
  std::string exposed_headers;

  /** @brief Preflight cache lifetime; zero omits Access-Control-Max-Age. */
  std::chrono::seconds max_age{};

  /** @brief Whether every Origin value is allowed. */
  bool allow_any_origin{};

  /** @brief Whether credentialed cross-origin requests are allowed. */
  bool allow_credentials{};
};

/** @brief Middleware that applies a CORS policy and terminates matching preflight requests. */
class SERVEZA_WEB_API cors final {
public:
  /** @brief Creates CORS middleware using @p options. */
  explicit cors(cors_options options);

  /** @brief Applies CORS response headers, handles preflight, or continues the chain. */
  void operator()(request_context& ctx, continuation& next, completion_handler handler) const;

private:
  [[nodiscard]] bool allows(std::string_view origin) const noexcept;
  [[nodiscard]] bool allows_method(std::string_view method) const noexcept;
  [[nodiscard]] bool allows_headers(std::string_view headers) const noexcept;

  cors_options m_options;
  std::vector<std::string> m_allowed_methods;
  std::vector<std::string> m_allowed_headers;
  bool m_allow_any_header{};
};

} // namespace serveza::web::middleware

#endif // SERVEZA_WEB_MIDDLEWARE_CORS_H
