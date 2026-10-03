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

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>

#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/io_context.hpp>

#include <gtest/gtest.h>

#include <serveza/web/layer.h>
#include <serveza/web/middleware.h>
#include <serveza/web/middleware/access_log.h>
#include <serveza/web/middleware/request_id.h>

#include "RequestHarness.h"

namespace {

template<typename T>
class CountingAllocator {
public:
  using value_type = T;

  explicit CountingAllocator(std::shared_ptr<std::size_t> allocations) noexcept
    : m_allocations{std::move(allocations)}
  {}

  template<typename U>
  CountingAllocator(const CountingAllocator<U>& other) noexcept
    : m_allocations{other.allocations()}
  {}

  [[nodiscard]] T* allocate(std::size_t size)
  {
    ++*m_allocations;
    return std::allocator<T>{}.allocate(size);
  }

  void deallocate(T* value, std::size_t size) noexcept
  {
    std::allocator<T>{}.deallocate(value, size);
  }

  [[nodiscard]] const std::shared_ptr<std::size_t>& allocations() const noexcept
  {
    return m_allocations;
  }

  template<typename U>
  friend bool operator==(const CountingAllocator& lhs, const CountingAllocator<U>& rhs) noexcept
  {
    return lhs.m_allocations == rhs.allocations();
  }

  template<typename U>
  friend bool operator!=(const CountingAllocator& lhs, const CountingAllocator<U>& rhs) noexcept
  {
    return !(lhs == rhs);
  }

private:
  std::shared_ptr<std::size_t> m_allocations;
};

TEST(ContinuationTest, InvokesNextExactlyOnce)
{
  struct State {
    int calls{};
    void operator()()
    {
      ++calls;
    }
  } state;

  auto next = serveza::web::details::continuation_access::make(
      &state, [](void* value, serveza::web::completion_handler handler) {
        (*static_cast<State*>(value))();
        handler(boost::system::error_code{});
      });
  EXPECT_FALSE(next.called());
  bool completed{};
  next([&](boost::system::error_code ec) {
    EXPECT_FALSE(ec);
    completed = true;
  });
  EXPECT_TRUE(next.called());
  EXPECT_TRUE(completed);
  EXPECT_EQ(state.calls, 1);
  EXPECT_THROW(next([](boost::system::error_code) {}), std::logic_error);
  EXPECT_EQ(state.calls, 1);
}

TEST(MiddlewareApiTest, ContinuationCannotBeCopiedOrMoved)
{
  static_assert(!std::is_copy_constructible_v<serveza::web::continuation>);
  static_assert(!std::is_move_constructible_v<serveza::web::continuation>);
  static_assert(!std::is_copy_assignable_v<serveza::web::continuation>);
  static_assert(!std::is_move_assignable_v<serveza::web::continuation>);
}

TEST(HandlerAdapterTest, PreservesAssociationsAndNeverCompletesInline)
{
  RequestHarness request;
  auto ctx = request.state().make_context();
  boost::asio::io_context completionIo;
  boost::asio::cancellation_signal signal;
  const auto allocations = std::make_shared<std::size_t>();
  bool observedExecutor{};
  bool observedCancellation{};
  bool completed{};

  auto middleware = [&](serveza::web::request_context& current, serveza::web::completion_handler done) {
    observedExecutor =
        boost::asio::get_associated_executor(done, current.get_executor()) == completionIo.get_executor();
    observedCancellation = boost::asio::get_associated_cancellation_slot(done).is_connected();
    done(boost::system::error_code{});
  };
  serveza::web::async_invoke_layer(
      middleware, ctx,
      boost::asio::bind_allocator(
          CountingAllocator<void>{allocations},
          boost::asio::bind_cancellation_slot(
              signal.slot(), boost::asio::bind_executor(completionIo.get_executor(), [&](boost::system::error_code ec) {
                EXPECT_FALSE(ec);
                completed = true;
              }))));

  EXPECT_TRUE(observedExecutor);
  EXPECT_TRUE(observedCancellation);
  EXPECT_FALSE(completed);
  EXPECT_GT(*allocations, 0U);
  request.io.run();
  EXPECT_FALSE(completed);
  completionIo.run();
  EXPECT_TRUE(completed);
}

TEST(BuiltinMiddlewareValidationTest, RejectsInvalidConstruction)
{
  using AccessLogSink = void (*)(const serveza::web::middleware::access_log_entry&);
  EXPECT_THROW(serveza::web::middleware::access_log{static_cast<AccessLogSink>(nullptr)}, std::invalid_argument);

  serveza::web::middleware::request_id_options options;
  options.header.clear();
  EXPECT_THROW((void)serveza::web::middleware::request_id{options}, std::invalid_argument);
}

} // namespace
