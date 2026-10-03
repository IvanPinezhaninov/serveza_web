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

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <serveza/web/target.h>

#include "detail/core.h"

namespace {

TEST(TargetParserTest, SplitsPathAndQueryWithoutDecoding)
{
  const auto target = serveza::web::details::parse_target("/search%20here?tag=a%2Fb&empty=");

  EXPECT_EQ(target.raw, "/search%20here?tag=a%2Fb&empty=");
  EXPECT_EQ(target.path, "/search%20here");
  EXPECT_EQ(target.query.encoded(), "tag=a%2Fb&empty=");
  EXPECT_EQ(target.query.first("tag"), "a%2Fb");
}

TEST(TargetParserTest, NormalizesAnEmptyTargetToRoot)
{
  const auto target = serveza::web::details::parse_target("");
  EXPECT_EQ(target.raw, "/");
  EXPECT_EQ(target.path, "/");
}

TEST(TargetParserTest, AcceptsAsteriskForm)
{
  const auto target = serveza::web::details::parse_target("*");
  EXPECT_EQ(target.path, "*");
  EXPECT_TRUE(target.query.encoded().empty());
}

TEST(TargetParserTest, RejectsUnsupportedOrMalformedForms)
{
  EXPECT_THROW(serveza::web::details::parse_target("https://example.test/path"), std::invalid_argument);
  EXPECT_THROW(serveza::web::details::parse_target("example.test:443"), std::invalid_argument);
  EXPECT_THROW(serveza::web::details::parse_target("/path#fragment"), std::invalid_argument);
  EXPECT_THROW(serveza::web::details::parse_target("/bad%2"), std::invalid_argument);
  EXPECT_THROW(serveza::web::details::parse_target("/bad%xx"), std::invalid_argument);
  EXPECT_THROW(serveza::web::details::parse_target("/path?bad%"), std::invalid_argument);
}

TEST(QueryParamsViewTest, PreservesOrderDuplicatesAndEmptyValues)
{
  serveza::web::query_params_view query{"tag=a&empty=&tag=b&flag&&=value"};
  std::vector<std::pair<std::string_view, std::string_view>> values;
  query.for_each([&](std::string_view name, std::string_view value) { values.emplace_back(name, value); });

  EXPECT_EQ(values, (std::vector<std::pair<std::string_view, std::string_view>>{
                        {"tag", "a"}, {"empty", ""}, {"tag", "b"}, {"flag", ""}, {"", "value"}}));
  EXPECT_EQ(query.first("tag"), "a");
  EXPECT_EQ(query.first("empty"), "");
  EXPECT_FALSE(query.first("missing"));
}

TEST(PercentDecodeTest, DecodesEscapesLazilyAndExactlyOnce)
{
  EXPECT_EQ(serveza::web::percent_decode("plain"), "plain");
  EXPECT_EQ(serveza::web::percent_decode("caf%C3%A9"), "caf\xC3\xA9");
  EXPECT_EQ(serveza::web::percent_decode("a%2Fb"), "a/b");
  EXPECT_EQ(serveza::web::percent_decode("value%2523"), "value%23");
  EXPECT_EQ(serveza::web::percent_decode("a+b"), "a+b");
}

TEST(PercentDecodeTest, RejectsMalformedEscapes)
{
  EXPECT_THROW(serveza::web::percent_decode("%"), std::invalid_argument);
  EXPECT_THROW(serveza::web::percent_decode("%0"), std::invalid_argument);
  EXPECT_THROW(serveza::web::percent_decode("%xz"), std::invalid_argument);
}

} // namespace
