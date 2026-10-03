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

#include <atomic>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "detail/route_pattern.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define SERVEZA_WEB_SANITIZER 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define SERVEZA_WEB_SANITIZER 1
#endif

namespace {

using Pattern = serveza::web::details::compiled_pattern;

std::atomic_size_t allocationCount{};
bool countAllocations = false;

struct MatchCase {
  std::string pattern;
  std::string path;
  bool matched{};
  std::vector<std::pair<std::string, std::string>> params;
};

class RoutePatternLiteralTest : public testing::TestWithParam<MatchCase> {};
class RoutePatternParameterTest : public testing::TestWithParam<MatchCase> {};
class RoutePatternOptionalTest : public testing::TestWithParam<MatchCase> {};
class RoutePatternWildcardTest : public testing::TestWithParam<MatchCase> {};
class RoutePatternConstraintTest : public testing::TestWithParam<MatchCase> {};

void expectMatch(const MatchCase& test)
{
  serveza::web::route_params params;
  Pattern pattern{test.pattern, Pattern::mode::exact, {}};
  ASSERT_EQ(pattern.match(test.path, params), test.matched);
  ASSERT_EQ(params.size(), test.params.size());
  for (std::size_t i = 0; i < test.params.size(); ++i) {
    EXPECT_EQ(params.begin()[i].name, test.params[i].first);
    EXPECT_EQ(params.begin()[i].value, test.params[i].second);
  }
}

TEST_P(RoutePatternLiteralTest, MatchesPath)
{
  expectMatch(GetParam());
}

TEST_P(RoutePatternParameterTest, MatchesPath)
{
  expectMatch(GetParam());
}

TEST_P(RoutePatternOptionalTest, MatchesPath)
{
  expectMatch(GetParam());
}

TEST_P(RoutePatternWildcardTest, MatchesPath)
{
  expectMatch(GetParam());
}

TEST_P(RoutePatternConstraintTest, MatchesPath)
{
  expectMatch(GetParam());
}

INSTANTIATE_TEST_SUITE_P(Compatibility, RoutePatternLiteralTest,
                         testing::Values(MatchCase{"", "/", true, {}}, MatchCase{"/", "/", true, {}},
                                         MatchCase{"/", "/x", false, {}}, MatchCase{"/foo", "/", false, {}},
                                         MatchCase{"/foo", "/foo", true, {}}, MatchCase{"/foo", "/foo/", true, {}},
                                         MatchCase{"/foo/", "/foo", true, {}}, MatchCase{"/foo/", "/foo/", true, {}},
                                         MatchCase{"/foo", "/FOO", false, {}}, MatchCase{"/foo", "/foo/bar", false, {}},
                                         MatchCase{"foo/bar", "/foo/bar", true, {}},
                                         MatchCase{"/caf\xC3\xA9", "/caf\xC3\xA9", true, {}},
                                         MatchCase{"/a%2Fb", "/a%2Fb", true, {}},
                                         MatchCase{"/a%2Fb", "/a/b", false, {}}));

INSTANTIATE_TEST_SUITE_P(
    Compatibility, RoutePatternParameterTest,
    testing::Values(
        MatchCase{"/:foo", "/", false, {}}, MatchCase{"/:foo", "/x", true, {{"foo", "x"}}},
        MatchCase{"/:foo", "/x/", true, {{"foo", "x"}}}, MatchCase{"/:foo", "/x/y", false, {}},
        MatchCase{"/:foo/:bar", "/x", false, {}}, MatchCase{"/:foo/:bar", "/x/y", true, {{"foo", "x"}, {"bar", "y"}}},
        MatchCase{"/:foo/:bar", "/x/y/", true, {{"foo", "x"}, {"bar", "y"}}},
        MatchCase{"/:foo/:bar", "/x/y/z", false, {}}, MatchCase{"/foo/:bar", "/foo/y", true, {{"bar", "y"}}},
        MatchCase{"/:foo/bar", "/x/bar", true, {{"foo", "x"}}}, MatchCase{"/:foo", "/a%2Fb", true, {{"foo", "a%2Fb"}}},
        MatchCase{"/:scope/:id", "/api/42", true, {{"scope", "api"}, {"id", "42"}}},
        MatchCase{"/:name.:ext", "/archive.tar", true, {{"name", "archive"}, {"ext", "tar"}}},
        MatchCase{"/:name.:ext", "/archive.tar.gz", true, {{"name", "archive"}, {"ext", "tar.gz"}}},
        MatchCase{"/:name.:ext", "/archive", false, {}}, MatchCase{"/:name.:ext", "/.hidden", false, {}},
        MatchCase{"/download/:name.:ext", "/download/archive.zip", true, {{"name", "archive"}, {"ext", "zip"}}},
        MatchCase{"/download/file.:ext", "/download/file.zip", true, {{"ext", "zip"}}},
        MatchCase{"/download/:name.json", "/download/report.json", true, {{"name", "report"}}},
        MatchCase{"/download/:name.json", "/download/.json", false, {}},
        MatchCase{"/v:version", "/v2", true, {{"version", "2"}}},
        MatchCase{"/:first-:second-:third", "/a-b-c", true, {{"first", "a"}, {"second", "b"}, {"third", "c"}}}));

INSTANTIATE_TEST_SUITE_P(
    SegmentSyntax, RoutePatternOptionalTest,
    testing::Values(MatchCase{"/users/:id?", "/users", true, {}},
                    MatchCase{"/users/:id?", "/users/7", true, {{"id", "7"}}},
                    MatchCase{"/users/:id?", "/users/7/extra", false, {}},
                    MatchCase{"/:locale?/users/:id", "/users/7", true, {{"id", "7"}}},
                    MatchCase{"/:locale?/users/:id", "/en/users/7", true, {{"locale", "en"}, {"id", "7"}}},
                    MatchCase{"/:first?/:second?/:required", "/value", true, {{"required", "value"}}},
                    MatchCase{
                        "/:first?/:second?/:required", "/one/value", true, {{"first", "one"}, {"required", "value"}}},
                    MatchCase{"/:first?/:second?/:required",
                              "/one/two/value",
                              true,
                              {{"first", "one"}, {"second", "two"}, {"required", "value"}}},
                    MatchCase{"/rooms/:roomId/state/:eventType{/:stateKey}",
                              "/rooms/room/state/topic",
                              true,
                              {{"roomId", "room"}, {"eventType", "topic"}}},
                    MatchCase{"/rooms/:roomId/state/:eventType{/:stateKey}",
                              "/rooms/room/state/topic/name",
                              true,
                              {{"roomId", "room"}, {"eventType", "topic"}, {"stateKey", "name"}}},
                    MatchCase{"/download/:serverName/:mediaId{/:fileName}",
                              "/download/example.org/abc",
                              true,
                              {{"serverName", "example.org"}, {"mediaId", "abc"}}},
                    MatchCase{"/download/:serverName/:mediaId{/:fileName}",
                              "/download/example.org/abc/photo.jpg",
                              true,
                              {{"serverName", "example.org"}, {"mediaId", "abc"}, {"fileName", "photo.jpg"}}},
                    MatchCase{"/download/:serverName/:mediaId{/:fileName}", "/download/example.org/abc/a/b", false, {}},
                    MatchCase{"/items{/:id([0-9]{2})}", "/items", true, {}},
                    MatchCase{"/items{/:id([0-9]{2})}", "/items/42", true, {{"id", "42"}}},
                    MatchCase{"/items{/:id([0-9]{2})}", "/items/4", false, {}}));

INSTANTIATE_TEST_SUITE_P(
    Compatibility, RoutePatternWildcardTest,
    testing::Values(
        MatchCase{"/*path", "/", false, {}}, MatchCase{"/*path", "/x", true, {{"path", "x"}}},
        MatchCase{"/*path", "/x/", true, {{"path", "x"}}}, MatchCase{"/*path", "/x/y", true, {{"path", "x/y"}}},
        MatchCase{"/files/*path", "/files", false, {}}, MatchCase{"/files/*path", "/files/a", true, {{"path", "a"}}},
        MatchCase{"/files/*path", "/files/a/b.txt", true, {{"path", "a/b.txt"}}},
        MatchCase{"/:bucket/*path", "/images/icons/a.svg", true, {{"bucket", "images"}, {"path", "icons/a.svg"}}}));

INSTANTIATE_TEST_SUITE_P(
    SegmentLocal, RoutePatternConstraintTest,
    testing::Values(MatchCase{"/:value(\\d{3})", "/1", false, {}},
                    MatchCase{"/:value(\\d{3})", "/111", true, {{"value", "111"}}},
                    MatchCase{"/:value([0-9]+)", "/12345", true, {{"value", "12345"}}},
                    MatchCase{"/:value([0-9]+)", "/12a", false, {}},
                    MatchCase{"/:value([0-9a-fA-F]+)", "/01aB", true, {{"value", "01aB"}}},
                    MatchCase{"/:value([0-9a-fA-F]+)", "/xyz", false, {}},
                    MatchCase{"/:value(uuid)",
                              "/123e4567-e89b-12d3-a456-426614174000",
                              true,
                              {{"value", "123e4567-e89b-12d3-a456-426614174000"}}},
                    MatchCase{"/:value(uuid)", "/123e4567e89b12d3a456426614174000", false, {}},
                    MatchCase{"/:value(open|closed|pending)", "/closed", true, {{"value", "closed"}}},
                    MatchCase{"/:value(open|closed|pending)", "/other", false, {}},
                    MatchCase{"/:value([a-z]{2}[0-9]{2})", "/ab12", true, {{"value", "ab12"}}},
                    MatchCase{"/:value([a-z]{2}[0-9]{2})", "/AB12", false, {}},
                    MatchCase{"/v:version([0-9]+)", "/v12", true, {{"version", "12"}}},
                    MatchCase{"/v:version([0-9]+)", "/vnext", false, {}},
                    MatchCase{"/:name([a-z]+)-:id([0-9]+)", "/item-42", true, {{"name", "item"}, {"id", "42"}}},
                    MatchCase{"/:name([a-z]+)-:id([0-9]+)", "/item-other", false, {}},
                    MatchCase{"/:kind((foo|bar))-:id", "/bar-7", true, {{"kind", "bar"}, {"id", "7"}}}));

TEST(RoutePatternPolicyTest, PrefixStopsAtSegmentBoundary)
{
  serveza::web::route_params params;
  Pattern pattern{"/api", Pattern::mode::prefix, {}};

  EXPECT_TRUE(pattern.match("/api", params));
  EXPECT_TRUE(pattern.match("/api/", params));
  EXPECT_TRUE(pattern.match("/api/users", params));
  EXPECT_FALSE(pattern.match("/apiary", params));
}

TEST(RoutePatternPolicyTest, StrictTrailingSlashDistinguishesForms)
{
  serveza::web::settings settings;
  settings.trailing_slash = serveza::web::trailing_slash_policy::strict;
  serveza::web::route_params params;
  Pattern withSlash{"/users/", Pattern::mode::exact, settings};
  Pattern withoutSlash{"/users", Pattern::mode::exact, settings};

  EXPECT_TRUE(withSlash.match("/users/", params));
  EXPECT_FALSE(withSlash.match("/users", params));
  EXPECT_TRUE(withoutSlash.match("/users", params));
  EXPECT_FALSE(withoutSlash.match("/users/", params));
}

TEST(RoutePatternPolicyTest, RejectsInvalidRequestPathsAndClearsCaptures)
{
  serveza::web::route_params params;
  Pattern pattern{"/:value", Pattern::mode::exact, {}};
  ASSERT_TRUE(pattern.match("/captured", params));

  EXPECT_FALSE(pattern.match("", params));
  EXPECT_TRUE(params.empty());
  EXPECT_FALSE(pattern.match("without-leading-slash", params));
  EXPECT_TRUE(params.empty());
  EXPECT_FALSE(pattern.match("//", params));
  EXPECT_TRUE(params.empty());
  EXPECT_FALSE(pattern.match("/a//b", params));
  EXPECT_TRUE(params.empty());
}

TEST(RoutePatternPolicyTest, HonorsRegexSegmentLimit)
{
  serveza::web::settings settings;
  settings.regex_segment_limit = 3;
  serveza::web::route_params params;
  Pattern pattern{"/:value([a-z]+)", Pattern::mode::exact, settings};

  EXPECT_TRUE(pattern.match("/abc", params));
  EXPECT_FALSE(pattern.match("/abcd", params));
}

TEST(RoutePatternIndexTest, ExposesOnlyACompleteLeadingLiteral)
{
  EXPECT_EQ(Pattern("/users/:id", Pattern::mode::exact, {}).first_literal(), "users");
  EXPECT_FALSE(Pattern("/:scope/:id", Pattern::mode::exact, {}).first_literal());
  EXPECT_FALSE(Pattern("/*path", Pattern::mode::exact, {}).first_literal());
}

TEST(RoutePatternIndexTest, ExposesMandatoryLiteralAndVariableSegments)
{
  const Pattern pattern{"/:tenant/users/:id/details", Pattern::mode::exact, {}};
  ASSERT_EQ(pattern.indexable_segment_count(), 4U);
  EXPECT_FALSE(pattern.indexable_literal(0));
  EXPECT_EQ(pattern.indexable_literal(1), "users");
  EXPECT_FALSE(pattern.indexable_literal(2));
  EXPECT_EQ(pattern.indexable_literal(3), "details");
  EXPECT_FALSE(pattern.indexable_literal(4));

  const Pattern optional{"/users/:id?/details", Pattern::mode::exact, {}};
  ASSERT_EQ(optional.indexable_segment_count(), 1U);
  EXPECT_EQ(optional.indexable_literal(0), "users");

  const Pattern wildcard{"/assets/*path", Pattern::mode::exact, {}};
  ASSERT_EQ(wildcard.indexable_segment_count(), 1U);
  EXPECT_EQ(wildcard.indexable_literal(0), "assets");
}

TEST(RoutePatternValidationTest, RejectsMalformedPatterns)
{
  const serveza::web::settings settings;
  EXPECT_THROW(Pattern("/a//b", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/a/*rest/b", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/:id/:id", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/:id/*id", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/:value(", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/:value()", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/:value(*)", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/::name", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/*", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/{/:value}", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/file{.:ext}", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/foo?", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/foo*bar", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/:first:second", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/file.:ext?", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/:id-:id", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/files{/:name", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/files{:name}", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/files{/one/two}", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/files{/literal}", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/files{/:first{/:second}}", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_THROW(Pattern("/files/:name}", Pattern::mode::exact, settings), std::invalid_argument);
  EXPECT_NO_THROW(Pattern("/items{/:id(\\d+)}", Pattern::mode::exact, settings));
}

TEST(RoutePatternValidationTest, RejectsMoreParametersThanTheResultCanStore)
{
  std::string pattern;
  for (std::size_t i = 0; i <= serveza::web::route_params::capacity; ++i)
    pattern += "/:p" + std::to_string(i);

  EXPECT_THROW(Pattern(pattern, Pattern::mode::exact, {}), std::invalid_argument);
}

TEST(RoutePatternAllocationTest, OrdinaryAndFastConstraintMatchesDoNotAllocate)
{
  Pattern ordinary{"/api/accounts/:account/orders/:order", Pattern::mode::exact, {}};
  Pattern numeric{"/users/:id([0-9]+)", Pattern::mode::exact, {}};
  Pattern composite{"/files/:name.:ext", Pattern::mode::exact, {}};
  serveza::web::route_params params;
  ASSERT_TRUE(ordinary.match("/api/accounts/0123456789/orders/42", params));
  ASSERT_TRUE(numeric.match("/users/12345", params));
  ASSERT_TRUE(composite.match("/files/archive.tar.gz", params));

  allocationCount.store(0, std::memory_order_relaxed);
  countAllocations = true;
  const bool ordinaryHit = ordinary.match("/api/accounts/0123456789/orders/42", params);
  const bool ordinaryMiss = ordinary.match("/api/clients/0123456789/orders/42", params);
  const bool numericHit = numeric.match("/users/12345", params);
  const bool compositeHit = composite.match("/files/archive.tar.gz", params);
  countAllocations = false;

  EXPECT_TRUE(ordinaryHit);
  EXPECT_FALSE(ordinaryMiss);
  EXPECT_TRUE(numericHit);
  EXPECT_TRUE(compositeHit);
#if !defined(SERVEZA_WEB_SANITIZER)
  EXPECT_EQ(allocationCount.load(std::memory_order_relaxed), 0U);
#endif
}

} // namespace

#if !defined(SERVEZA_WEB_SANITIZER)
#if defined(__GNUC__) || defined(__clang__)
#define SERVEZA_WEB_NOINLINE __attribute__((noinline))
#else
#define SERVEZA_WEB_NOINLINE
#endif

SERVEZA_WEB_NOINLINE void* operator new(std::size_t size)
{
  if (countAllocations) allocationCount.fetch_add(1, std::memory_order_relaxed);
  if (void* value = std::malloc(size)) return value;
  throw std::bad_alloc{};
}

SERVEZA_WEB_NOINLINE void operator delete(void* value) noexcept
{
  std::free(value);
}

SERVEZA_WEB_NOINLINE void operator delete(void* value, std::size_t) noexcept
{
  std::free(value);
}

#undef SERVEZA_WEB_NOINLINE
#endif
