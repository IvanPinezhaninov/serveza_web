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

#ifndef SERVEZA_WEB_LAYER_H
#define SERVEZA_WEB_LAYER_H

#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

#include <boost/system/errc.hpp>
#include <boost/system/system_error.hpp>

#include <serveza/web/async.h>
#include <serveza/web/context.h>
#include <serveza/web/middleware.h>

namespace serveza::web {

/// @cond INTERNAL
namespace details {

boost::system::error_code layer_exception_to_error(std::exception_ptr ep) noexcept;

template<typename Handler>
class handler_completion_state final {
public:
  explicit handler_completion_state(Handler handler)
    : m_handler{std::move(handler)}
  {}

  [[nodiscard]] const Handler& associated_handler() const noexcept
  {
    return *m_handler;
  }

  void complete(boost::asio::any_io_executor fallback, boost::system::error_code ec)
  {
    std::optional<Handler> handler;
    {
      std::lock_guard lock{m_mutex};
      if (!m_handler) return;
      handler.emplace(std::move(*m_handler));
      m_handler.reset();
    }
    post_completion(std::move(fallback), std::move(*handler), ec);
  }

private:
  std::mutex m_mutex;
  std::optional<Handler> m_handler;
};

template<typename Handler>
auto make_handler_completion(boost::asio::any_io_executor fallback, Handler handler)
{
  using state_type = handler_completion_state<Handler>;
  std::shared_ptr<state_type> state;
  if constexpr (std::is_same_v<std::decay_t<Handler>, completion_handler>) {
    state = std::make_shared<state_type>(std::move(handler));
  } else {
    auto allocator = boost::asio::get_associated_allocator(handler);
    state = std::allocate_shared<state_type>(allocator, std::move(handler));
  }
  auto completion =
      bind_associated(fallback, state->associated_handler(), [state, fallback](boost::system::error_code ec) mutable {
        state->complete(std::move(fallback), ec);
      });
  return std::pair{std::move(state), std::move(completion)};
}
/// @endcond
} // namespace details

/** Invokes a callback middleware through an arbitrary CompletionToken. */
template<typename Callable, typename CompletionToken>
auto async_invoke_layer(Callable& callable, request_context& ctx, continuation& next, CompletionToken&& token)
{
  return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
      [&callable, &ctx, &next](auto handler) mutable {
        auto [state, completion] = details::make_handler_completion(ctx.get_executor(), std::move(handler));
        try {
          callable(ctx, next, completion_handler{std::move(completion)});
        } catch (...) {
          auto ep = std::current_exception();
          ctx.report_exception(ep);
          state->complete(ctx.get_executor(), details::layer_exception_to_error(ep));
        }
      },
      token);
}

/** Invokes a terminal callback layer through an arbitrary CompletionToken. */
template<typename Callable, typename CompletionToken>
auto async_invoke_layer(Callable& callable, request_context& ctx, CompletionToken&& token)
{
  return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
      [&callable, &ctx](auto handler) mutable {
        auto [state, completion] = details::make_handler_completion(ctx.get_executor(), std::move(handler));
        try {
          callable(ctx, completion_handler{std::move(completion)});
        } catch (...) {
          auto ep = std::current_exception();
          ctx.report_exception(ep);
          state->complete(ctx.get_executor(), details::layer_exception_to_error(ep));
        }
      },
      token);
}

namespace details {

inline boost::system::error_code layer_exception_to_error(std::exception_ptr ep) noexcept
{
  if (!ep) return {};
  try {
    std::rethrow_exception(ep);
  } catch (const boost::system::system_error& e) {
    return e.code();
  } catch (...) {
    return make_error_code(boost::system::errc::io_error);
  }
}

} // namespace details

/** @brief Explicit wrapper for a callback route or middleware callable. */
template<typename Function>
class callback_layer final {
public:
  /** @brief Stores the callback callable. */
  explicit callback_layer(Function function)
    : m_function{std::move(function)}
  {}

  /** @brief Forwards arguments to the stored callable. */
  template<typename... Args>
  decltype(auto) operator()(Args&&... args)
  {
    return m_function(std::forward<Args>(args)...);
  }

private:
  Function m_function;
};

template<typename Function>
callback_layer(Function) -> callback_layer<Function>;

} // namespace serveza::web

#endif // SERVEZA_WEB_LAYER_H
