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

#ifndef SERVEZA_WEB_MIDDLEWARE_REQUEST_BODY_LIMIT_H
#define SERVEZA_WEB_MIDDLEWARE_REQUEST_BODY_LIMIT_H

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <serveza/web/async.h>
#include <serveza/web/export.h>

namespace serveza::web {
class continuation;
class request_context;
} // namespace serveza::web

namespace serveza::web::middleware {

namespace details {

SERVEZA_WEB_API void apply_request_body_limit(request_context& ctx, continuation& next, completion_handler handler,
                                              std::size_t limit);

} // namespace details

/** @brief Middleware that applies one fixed request body limit. */
class SERVEZA_WEB_API request_body_limit final {
public:
  /**
   * @brief Creates middleware applying @p limit bytes.
   *
   * The limit must not exceed @ref settings::body_limit when the middleware
   * runs and must be applied before body consumption starts.
   */
  explicit request_body_limit(std::size_t limit);

  /** @brief Applies the configured limit and continues the middleware chain. */
  void operator()(request_context& ctx, continuation& next, completion_handler handler) const;

private:
  std::size_t m_limit;
};

/**
 * @brief Middleware that selects a request body limit from request metadata.
 *
 * The selector is stored directly, without @c std::function, and must accept
 * a @c const @c request_context& and return a byte count. It may be invoked
 * concurrently for different requests.
 */
template<typename Selector>
class request_body_limit_by final {
public:
  /** @brief Stores @p selector by value. */
  explicit request_body_limit_by(Selector selector)
    : m_selector{std::move(selector)}
  {
    static_assert(std::is_invocable_r_v<std::size_t, const Selector&, const request_context&>,
                  "body limit selector must be callable with (const request_context&)");
    if constexpr (std::is_pointer_v<Selector>) {
      if (!m_selector) throw std::invalid_argument{"request body limit selector must not be null"};
    }
  }

  /** @brief Selects and applies the limit, then continues the middleware chain. */
  void operator()(request_context& ctx, continuation& next, completion_handler handler) const
  {
    details::apply_request_body_limit(ctx, next, std::move(handler), m_selector(ctx));
  }

private:
  Selector m_selector;
};

} // namespace serveza::web::middleware

#endif // SERVEZA_WEB_MIDDLEWARE_REQUEST_BODY_LIMIT_H
