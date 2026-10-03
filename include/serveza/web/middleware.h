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

#ifndef SERVEZA_WEB_MIDDLEWARE_H
#define SERVEZA_WEB_MIDDLEWARE_H

#include <utility>

#include <boost/asio/async_result.hpp>

#include <serveza/web/async.h>
#include <serveza/web/export.h>

namespace serveza::web {

/// @cond INTERNAL
namespace details {
[[noreturn]] SERVEZA_WEB_API void throw_continuation_already_called();
class continuation_access;
} // namespace details
/// @endcond

/**
 * @brief One-shot continuation passed to middleware.
 *
 * Invoke it with an Asio completion token to run the remainder of the matching
 * middleware chain. Calling the same continuation more than once throws
 * @c std::logic_error.
 */
class SERVEZA_WEB_API continuation final {
public:
  /** @brief Continuations cannot be copied. */
  continuation(const continuation&) = delete;

  /** @brief Continuations cannot be moved. */
  continuation(continuation&&) = delete;

  /** @brief Continuations cannot be copy-assigned. */
  continuation& operator=(const continuation&) = delete;

  /** @brief Continuations cannot be move-assigned. */
  continuation& operator=(continuation&&) = delete;

  /**
   * @brief Runs the remainder of the middleware chain exactly once.
   *
   * The completion signature is `void(boost::system::error_code)`.
   */
  template<typename CompletionToken>
  auto operator()(CompletionToken&& token)
  {
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this](auto handler) mutable {
          if (m_called) details::throw_continuation_already_called();
          m_called = true;
          m_callback(m_state, completion_handler{std::move(handler)});
        },
        token);
  }

  /** @brief Returns whether this continuation has already been invoked. */
  [[nodiscard]] bool called() const noexcept
  {
    return m_called;
  }

private:
  using callback_type = void (*)(void*, completion_handler);

  friend class details::continuation_access;

  continuation(void* state, callback_type callback) noexcept
    : m_state{state}
    , m_callback{callback}
  {}

  void* m_state;
  callback_type m_callback;
  bool m_called{};
};

/// @cond INTERNAL
namespace details {

class continuation_access final {
public:
  static continuation make(void* state, continuation::callback_type callback) noexcept
  {
    return continuation{state, callback};
  }

  static void reset(continuation& value, void* state, continuation::callback_type callback) noexcept
  {
    value.m_state = state;
    value.m_callback = callback;
    value.m_called = false;
  }
};

} // namespace details
/// @endcond

} // namespace serveza::web

#endif // SERVEZA_WEB_MIDDLEWARE_H
