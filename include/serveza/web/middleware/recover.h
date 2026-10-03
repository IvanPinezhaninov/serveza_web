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

#ifndef SERVEZA_WEB_MIDDLEWARE_RECOVER_H
#define SERVEZA_WEB_MIDDLEWARE_RECOVER_H

#include <exception>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <boost/beast/http/status.hpp>

#include <serveza/web/async.h>
#include <serveza/web/export.h>

namespace serveza::web {
class continuation;
class request_context;
} // namespace serveza::web

namespace serveza::web::middleware {

/** @brief Response policy for unexpected exceptions handled by @ref recover. */
struct recover_options {
  /** @brief Status returned for exceptions other than @ref request_error. */
  boost::beast::http::status status{boost::beast::http::status::internal_server_error};

  /** @brief Response body returned for unexpected exceptions. */
  std::string body{"Internal Server Error"};

  /** @brief Content-Type returned for unexpected exceptions. */
  std::string content_type{"text/plain; charset=utf-8"};

  /** @brief Whether an unexpected exception forces the HTTP connection closed. */
  bool close_connection{true};
};

/// @cond INTERNAL
namespace details {

struct ignore_exception final {
  void operator()(std::exception_ptr) const noexcept {}
};

using exception_callback = void (*)(void*, std::exception_ptr);

SERVEZA_WEB_API void invoke_recover(request_context& ctx, continuation& next, completion_handler handler,
                                    const recover_options& options, void* observer, exception_callback callback);

} // namespace details
/// @endcond

/**
 * @brief Converts request and application exceptions into HTTP responses.
 *
 * A @ref request_error supplies its own status and message. Other exceptions
 * use @ref recover_options and are reported to the optional typed observer.
 * The observer is stored directly and its own exceptions are ignored.
 */
template<typename Observer = details::ignore_exception>
class recover final {
public:
  static_assert(std::is_invocable_r_v<void, Observer&, std::exception_ptr>,
                "exception observer must be callable with (exception_ptr)");

  /** @brief Creates recovery middleware without an exception observer. */
  explicit recover(recover_options options = {})
    : m_options{std::move(options)}
  {
    static_assert(std::is_default_constructible_v<Observer>,
                  "an exception observer must be supplied when it is not default constructible");
  }

  /** @brief Creates recovery middleware that stores @p observer by value. */
  recover(recover_options options, Observer observer)
    : m_options{std::move(options)}
    , m_observer{std::move(observer)}
  {}

  /** @brief Runs the remaining chain and maps escaping exceptions to responses. */
  void operator()(request_context& ctx, continuation& next, completion_handler handler) const
  {
    details::invoke_recover(ctx, next, std::move(handler), m_options, std::addressof(m_observer), &notify);
  }

private:
  static void notify(void* observer, std::exception_ptr ep)
  {
    (*static_cast<Observer*>(observer))(std::move(ep));
  }

  recover_options m_options;
  mutable Observer m_observer;
};

} // namespace serveza::web::middleware

#endif // SERVEZA_WEB_MIDDLEWARE_RECOVER_H
