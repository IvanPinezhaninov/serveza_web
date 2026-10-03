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

#ifndef SERVEZA_WEB_AWAITABLE_H
#define SERVEZA_WEB_AWAITABLE_H

#include <boost/asio/detail/config.hpp>

#if defined(BOOST_ASIO_HAS_CO_AWAIT)

#include <exception>
#include <type_traits>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>

#include <serveza/web/layer.h>
#include <serveza/web/websocket.h>

namespace serveza::web {

/// @cond INTERNAL
namespace details {

template<typename Awaitable>
void spawn_awaitable_layer(request_context& ctx, Awaitable awaitable, completion_handler handler)
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
  boost::asio::co_spawn(ctx.get_executor(), std::move(awaitable), std::move(completion));
}

template<typename Awaitable>
void spawn_awaitable_operation(boost::asio::any_io_executor executor, Awaitable awaitable, completion_handler handler)
{
  auto cancellation = boost::asio::get_associated_cancellation_slot(handler);
  auto allocator = boost::asio::get_associated_allocator(handler);
  auto completion = boost::asio::bind_cancellation_slot(
      cancellation,
      boost::asio::bind_allocator(allocator, [handler = std::move(handler)](std::exception_ptr ep) mutable {
        handler(layer_exception_to_error(std::move(ep)));
      }));
  boost::asio::co_spawn(std::move(executor), std::move(awaitable), std::move(completion));
}

} // namespace details
/// @endcond

/** @brief Adapts a C++20 awaitable route or middleware to the callback dispatcher. */
template<typename Function>
class awaitable_layer final {
public:
  /** @brief Stores @p function by value. */
  explicit awaitable_layer(Function function)
    : m_function{std::move(function)}
  {}

  /** @brief Runs the stored middleware callable as a C++20 coroutine. */
  template<typename Value = Function,
           std::enable_if_t<
               std::is_invocable_r_v<boost::asio::awaitable<void>, Value&, request_context&, continuation&>, int> = 0>
  void operator()(request_context& ctx, continuation& next, completion_handler handler)
  {
    details::spawn_awaitable_layer(ctx, m_function(ctx, next), std::move(handler));
  }

  /** @brief Runs the stored terminal route callable as a C++20 coroutine. */
  template<typename Value = Function,
           std::enable_if_t<std::is_invocable_r_v<boost::asio::awaitable<void>, Value&, request_context&>, int> = 0>
  void operator()(request_context& ctx, completion_handler handler)
  {
    details::spawn_awaitable_layer(ctx, m_function(ctx), std::move(handler));
  }

private:
  Function m_function;
};

template<typename Function>
awaitable_layer(Function) -> awaitable_layer<Function>;

/** @brief Adapts a C++20 awaitable producer to the callback chunked-response core. */
template<typename Function>
class awaitable_chunk_producer final {
public:
  /** @brief Stores @p function by value. */
  explicit awaitable_chunk_producer(Function function)
    : m_function{std::move(function)}
  {}

  /** @brief Runs the stored chunk producer as a C++20 coroutine. */
  void operator()(response_writer& writer, completion_handler handler)
  {
    details::spawn_awaitable_operation(writer.get_executor(), m_function(writer), std::move(handler));
  }

private:
  Function m_function;
};

template<typename Function>
awaitable_chunk_producer(Function) -> awaitable_chunk_producer<Function>;

/** @brief Adapts a C++20 awaitable WebSocket endpoint to the callback core. */
template<typename Function>
class awaitable_websocket_endpoint final {
public:
  /** @brief Stores @p function by value. */
  explicit awaitable_websocket_endpoint(Function function)
    : m_function{std::move(function)}
  {}

  /** @brief Runs the stored WebSocket endpoint as a C++20 coroutine. */
  void operator()(websocket_connection& connection, completion_handler handler)
  {
    details::spawn_awaitable_operation(connection.get_executor(), m_function(connection), std::move(handler));
  }

private:
  Function m_function;
};

template<typename Function>
awaitable_websocket_endpoint(Function) -> awaitable_websocket_endpoint<Function>;

} // namespace serveza::web

#endif // defined(BOOST_ASIO_HAS_CO_AWAIT)

#endif // SERVEZA_WEB_AWAITABLE_H
