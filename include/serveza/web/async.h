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

#ifndef SERVEZA_WEB_ASYNC_H
#define SERVEZA_WEB_ASYNC_H

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <boost/asio/any_completion_handler.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/system/error_code.hpp>

namespace serveza::web {

/** @brief Type-erased callback used only at compiled asynchronous boundaries. */
using completion_handler = boost::asio::any_completion_handler<void(boost::system::error_code)>;

namespace details {

template<typename Handler, typename Function>
auto bind_associated(boost::asio::any_io_executor fallback, const Handler& handler, Function function)
{
  auto executor = boost::asio::get_associated_executor(handler, fallback);
  auto cancellation = boost::asio::get_associated_cancellation_slot(handler);
  if constexpr (std::is_same_v<std::decay_t<Handler>, completion_handler>) {
    return boost::asio::bind_cancellation_slot(cancellation,
                                               boost::asio::bind_executor(std::move(executor), std::move(function)));
  } else {
    auto allocator = boost::asio::get_associated_allocator(handler);
    return boost::asio::bind_cancellation_slot(
        cancellation,
        boost::asio::bind_executor(std::move(executor), boost::asio::bind_allocator(allocator, std::move(function))));
  }
}

template<typename Handler, typename State>
auto retain_completion(boost::asio::any_io_executor fallback, Handler handler, std::shared_ptr<State> state)
{
  auto executor = boost::asio::get_associated_executor(handler, fallback);
  auto cancellation = boost::asio::get_associated_cancellation_slot(handler);
  if constexpr (std::is_same_v<std::decay_t<Handler>, completion_handler>) {
    return boost::asio::bind_cancellation_slot(
        cancellation,
        boost::asio::bind_executor(std::move(executor),
                                   [handler = std::move(handler), state = std::move(state)](auto&&... args) mutable {
                                     static_cast<void>(state);
                                     handler(std::forward<decltype(args)>(args)...);
                                   }));
  } else {
    auto allocator = boost::asio::get_associated_allocator(handler);
    return boost::asio::bind_cancellation_slot(
        cancellation, boost::asio::bind_executor(
                          std::move(executor),
                          boost::asio::bind_allocator(allocator, [handler = std::move(handler),
                                                                  state = std::move(state)](auto&&... args) mutable {
                            static_cast<void>(state);
                            handler(std::forward<decltype(args)>(args)...);
                          })));
  }
}

template<typename Handler, typename Function>
auto transform_completion(boost::asio::any_io_executor fallback, Handler handler, Function function)
{
  auto executor = boost::asio::get_associated_executor(handler, fallback);
  auto cancellation = boost::asio::get_associated_cancellation_slot(handler);
  if constexpr (std::is_same_v<std::decay_t<Handler>, completion_handler>) {
    return boost::asio::bind_cancellation_slot(
        cancellation,
        boost::asio::bind_executor(std::move(executor), [handler = std::move(handler),
                                                         function = std::move(function)](auto&&... args) mutable {
          function(handler, std::forward<decltype(args)>(args)...);
        }));
  } else {
    auto allocator = boost::asio::get_associated_allocator(handler);
    return boost::asio::bind_cancellation_slot(
        cancellation, boost::asio::bind_executor(
                          std::move(executor), boost::asio::bind_allocator(
                                                   allocator, [handler = std::move(handler),
                                                               function = std::move(function)](auto&&... args) mutable {
                                                     function(handler, std::forward<decltype(args)>(args)...);
                                                   })));
  }
}

template<typename Handler, typename... Args>
void post_completion(boost::asio::any_io_executor fallback, Handler handler, Args&&... args)
{
  auto executor = boost::asio::get_associated_executor(handler, fallback);
  auto allocator = boost::asio::get_associated_allocator(handler);
  auto cancellation = boost::asio::get_associated_cancellation_slot(handler);
  auto values = std::make_tuple(std::forward<Args>(args)...);
  boost::asio::post(
      std::move(fallback),
      boost::asio::bind_allocator(allocator, [executor = std::move(executor), allocator, cancellation,
                                              handler = std::move(handler), values = std::move(values)]() mutable {
        boost::asio::dispatch(
            std::move(executor),
            boost::asio::bind_cancellation_slot(
                cancellation, boost::asio::bind_allocator(allocator, [handler = std::move(handler),
                                                                      values = std::move(values)]() mutable {
                  std::apply([&handler](auto&&... unpacked) { handler(std::forward<decltype(unpacked)>(unpacked)...); },
                             std::move(values));
                })));
      }));
}

template<typename Handler, typename... Args>
void dispatch_completion(boost::asio::any_io_executor fallback, Handler handler, Args&&... args)
{
  auto executor = boost::asio::get_associated_executor(handler, fallback);
  auto allocator = boost::asio::get_associated_allocator(handler);
  auto cancellation = boost::asio::get_associated_cancellation_slot(handler);
  auto values = std::make_tuple(std::forward<Args>(args)...);
  boost::asio::dispatch(
      std::move(executor),
      boost::asio::bind_cancellation_slot(
          cancellation,
          boost::asio::bind_allocator(allocator, [handler = std::move(handler), values = std::move(values)]() mutable {
            std::apply([&handler](auto&&... unpacked) { handler(std::forward<decltype(unpacked)>(unpacked)...); },
                       std::move(values));
          })));
}

} // namespace details

} // namespace serveza::web

#endif // SERVEZA_WEB_ASYNC_H
