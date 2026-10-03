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

#include <array>
#include <cstddef>
#include <memory>
#include <utility>

#include <boost/system/error_code.hpp>

#include <gtest/gtest.h>

#include "detail/completion_callback.h"

namespace {

using serveza::web::details::completion_callback;

TEST(CompletionCallbackTest, InvokesInlineCallableAfterMove)
{
  bool called{};
  completion_callback source{[&called](boost::system::error_code ec) {
    EXPECT_FALSE(ec);
    called = true;
  }};

  completion_callback target{std::move(source)};

  EXPECT_FALSE(source);
  ASSERT_TRUE(target);
  target({});
  EXPECT_TRUE(called);
}

TEST(CompletionCallbackTest, InvokesLargeCallableAfterMoveAssignment)
{
  struct LargeCallable final {
    std::array<std::byte, 128> storage{};
    bool* called{};

    void operator()(boost::system::error_code ec)
    {
      EXPECT_FALSE(ec);
      *called = true;
    }
  };

  bool called{};
  completion_callback source{LargeCallable{{}, &called}};
  completion_callback target;

  target = std::move(source);

  EXPECT_FALSE(source);
  ASSERT_TRUE(target);
  target({});
  EXPECT_TRUE(called);
}

TEST(CompletionCallbackTest, DestroysOwnedCallableExactlyOnce)
{
  struct TrackedCallable final {
    std::shared_ptr<int> instances;

    explicit TrackedCallable(std::shared_ptr<int> value)
      : instances{std::move(value)}
    {
      ++*instances;
    }

    TrackedCallable(const TrackedCallable&) = delete;

    TrackedCallable(TrackedCallable&& other) noexcept
      : instances{std::move(other.instances)}
    {}

    ~TrackedCallable()
    {
      if (instances) --*instances;
    }

    void operator()(boost::system::error_code) {}
  };

  auto instances = std::make_shared<int>();
  {
    completion_callback first{TrackedCallable{instances}};
    EXPECT_EQ(*instances, 1);

    completion_callback second{std::move(first)};
    EXPECT_EQ(*instances, 1);

    first = std::move(second);
    EXPECT_EQ(*instances, 1);
  }
  EXPECT_EQ(*instances, 0);
}

} // namespace
