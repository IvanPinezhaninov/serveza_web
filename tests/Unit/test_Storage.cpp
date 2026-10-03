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

#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <serveza/web/storage.h>

namespace {

TEST(StorageTest, OwnsAndReplacesTypedValues)
{
  serveza::web::storage storage;
  std::string& first = storage.emplace<std::string>("first");
  EXPECT_EQ(first, "first");
  EXPECT_EQ(storage.require<std::string>(), "first");

  std::string& second = storage.emplace<std::string>("second");
  EXPECT_EQ(second, "second");
  EXPECT_EQ(storage.require<std::string>(), "second");
}

TEST(StorageTest, SupportsMoveOnlyValues)
{
  serveza::web::storage storage;
  auto& value = storage.emplace<std::unique_ptr<int>>(std::make_unique<int>(42));

  ASSERT_NE(value, nullptr);
  EXPECT_EQ(*value, 42);
}

TEST(StorageTest, AttachesMutableAndConstValues)
{
  serveza::web::storage mutableStorage;
  int mutableValue = 7;
  mutableStorage.attach(mutableValue);
  mutableStorage.require<int>() = 9;
  EXPECT_EQ(mutableValue, 9);

  serveza::web::storage constStorage;
  const std::string constValue{"read-only"};
  constStorage.attach(constValue);
  EXPECT_EQ(std::as_const(constStorage).require<std::string>(), "read-only");
  EXPECT_EQ(constStorage.get<std::string>(), nullptr);
}

TEST(StorageTest, MissingAndErasedValuesAreReported)
{
  serveza::web::storage storage;
  EXPECT_EQ(storage.get<int>(), nullptr);
  EXPECT_THROW(storage.require<int>(), std::out_of_range);
  EXPECT_THROW(std::as_const(storage).require<int>(), std::out_of_range);

  storage.emplace<int>(42);
  storage.erase<int>();
  EXPECT_EQ(storage.get<int>(), nullptr);
}

} // namespace
