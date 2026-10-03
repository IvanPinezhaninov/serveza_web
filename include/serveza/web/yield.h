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

#ifndef SERVEZA_WEB_YIELD_H
#define SERVEZA_WEB_YIELD_H

#include <exception>
#include <type_traits>
#include <utility>

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/system/system_error.hpp>

#include <serveza/web/detail/yield_stack_allocator.h>
#include <serveza/web/layer.h>
#include <serveza/web/websocket.h>

namespace serveza::web {

/** Invokes callback middleware and preserves its exception through a stackful coroutine. */
template<typename Callable>
void async_invoke_layer(Callable& callable, request_context& ctx, continuation& next, boost::asio::yield_context yield)
{
  boost::system::error_code ec;
  async_invoke_layer(callable, ctx, next, boost::asio::redirect_error(yield, ec));
  if (!ec) return;
  if (auto ep = ctx.take_exception()) std::rethrow_exception(ep);
  throw boost::system::system_error{ec};
}

/** Invokes a terminal callback layer and preserves its exception through a stackful coroutine. */
template<typename Callable>
void async_invoke_layer(Callable& callable, request_context& ctx, boost::asio::yield_context yield)
{
  boost::system::error_code ec;
  async_invoke_layer(callable, ctx, boost::asio::redirect_error(yield, ec));
  if (!ec) return;
  if (auto ep = ctx.take_exception()) std::rethrow_exception(ep);
  throw boost::system::system_error{ec};
}

/// @cond INTERNAL
namespace details {

template<typename Function>
void spawn_yield_layer(request_context& ctx, Function function, completion_handler handler)
{
  auto cancellation = boost::asio::get_associated_cancellation_slot(handler);
  auto allocator = boost::asio::get_associated_allocator(handler);
  auto completion = boost::asio::bind_cancellation_slot(
      cancellation,
      boost::asio::bind_allocator(allocator, [&ctx, handler = std::move(handler)](std::exception_ptr ep) mutable {
        if (ep) {
          if (auto operation_ep = ctx.take_exception()) ep = std::move(operation_ep);
        }
        ctx.report_exception(ep);
        handler(layer_exception_to_error(std::move(ep)));
      }));
  boost::asio::spawn(ctx.get_executor(), boost::asio::allocator_arg_t{}, yield_stack_allocator{}, std::move(function),
                     std::move(completion));
}

template<typename Function>
void spawn_yield_operation(boost::asio::any_io_executor executor, Function function, completion_handler handler)
{
  auto cancellation = boost::asio::get_associated_cancellation_slot(handler);
  auto allocator = boost::asio::get_associated_allocator(handler);
  auto completion = boost::asio::bind_cancellation_slot(
      cancellation,
      boost::asio::bind_allocator(allocator, [handler = std::move(handler)](std::exception_ptr ep) mutable {
        handler(layer_exception_to_error(std::move(ep)));
      }));
  boost::asio::spawn(std::move(executor), boost::asio::allocator_arg_t{}, yield_stack_allocator{}, std::move(function),
                     std::move(completion));
}

} // namespace details
/// @endcond

/** @brief Adapts a C++17 stackful route or middleware to the callback dispatcher. */
template<typename Function>
class yield_layer final {
public:
  /** @brief Stores @p function by value. */
  explicit yield_layer(Function function)
    : m_function{std::move(function)}
  {}

  /** @brief Runs the stored middleware callable as a stackful coroutine. */
  template<
      typename Value = Function,
      std::enable_if_t<std::is_invocable_r_v<void, Value&, request_context&, continuation&, boost::asio::yield_context>,
                       int> = 0>
  void operator()(request_context& ctx, continuation& next, completion_handler handler)
  {
    details::spawn_yield_layer(
        ctx, [this, &ctx, &next](boost::asio::yield_context yield) { m_function(ctx, next, yield); },
        std::move(handler));
  }

  /** @brief Runs the stored terminal route callable as a stackful coroutine. */
  template<typename Value = Function,
           std::enable_if_t<std::is_invocable_r_v<void, Value&, request_context&, boost::asio::yield_context>, int> = 0>
  void operator()(request_context& ctx, completion_handler handler)
  {
    details::spawn_yield_layer(
        ctx, [this, &ctx](boost::asio::yield_context yield) { m_function(ctx, yield); }, std::move(handler));
  }

private:
  Function m_function;
};

template<typename Function>
yield_layer(Function) -> yield_layer<Function>;

/** @brief Adapts a stackful producer for `request_context::async_send_chunked`. */
template<typename Function>
class yield_chunk_producer final {
public:
  /** @brief Stores @p function by value. */
  explicit yield_chunk_producer(Function function)
    : m_function{std::move(function)}
  {}

  /** @brief Runs the stored chunk producer as a stackful coroutine. */
  void operator()(response_writer& writer, completion_handler handler)
  {
    details::spawn_yield_operation(
        writer.get_executor(), [this, &writer](boost::asio::yield_context yield) { m_function(writer, yield); },
        std::move(handler));
  }

private:
  Function m_function;
};

template<typename Function>
yield_chunk_producer(Function) -> yield_chunk_producer<Function>;

/** @brief Adapts a stackful WebSocket endpoint to the callback WebSocket core. */
template<typename Function>
class yield_websocket_endpoint final {
public:
  /** @brief Stores @p function by value. */
  explicit yield_websocket_endpoint(Function function)
    : m_function{std::move(function)}
  {}

  /** @brief Runs the stored WebSocket endpoint as a stackful coroutine. */
  void operator()(websocket_connection& connection, completion_handler handler)
  {
    details::spawn_yield_operation(
        connection.get_executor(),
        [this, &connection](boost::asio::yield_context yield) { m_function(connection, yield); }, std::move(handler));
  }

private:
  Function m_function;
};

template<typename Function>
yield_websocket_endpoint(Function) -> yield_websocket_endpoint<Function>;

} // namespace serveza::web

#endif // SERVEZA_WEB_YIELD_H
