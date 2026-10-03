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

#include <chrono>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>

#include <gtest/gtest.h>

#include <serveza/web/cookies.h>

namespace {

TEST(RequestCookiesViewTest, DefaultViewIsEmpty)
{
  const serveza::web::request_cookies_view cookies;

  EXPECT_FALSE(cookies.first("missing"));
  std::size_t count{};
  cookies.for_each([&](std::string_view, std::string_view) { ++count; });
  EXPECT_EQ(count, 0U);
}

TEST(RequestCookiesViewTest, PreservesHeaderAndCookieOrder)
{
  boost::beast::http::fields fields;
  fields.insert(boost::beast::http::field::cookie, "session=first; theme=dark; flag=");
  fields.insert(boost::beast::http::field::cookie, "session=second; malformed");
  serveza::web::request_cookies_view cookies{fields};
  std::vector<std::pair<std::string_view, std::string_view>> values;
  cookies.for_each([&](std::string_view name, std::string_view value) { values.emplace_back(name, value); });

  EXPECT_EQ(values, (std::vector<std::pair<std::string_view, std::string_view>>{
                        {"session", "first"}, {"theme", "dark"}, {"flag", ""}, {"session", "second"}}));
  EXPECT_EQ(cookies.first("session"), "first");
  EXPECT_FALSE(cookies.first("missing"));
}

TEST(RequestCookiesViewTest, TrimsOptionalWhitespaceWithoutDecoding)
{
  boost::beast::http::fields fields;
  fields.insert(boost::beast::http::field::cookie, "  encoded=a%2Fb \t; spaced = value ");
  serveza::web::request_cookies_view cookies{fields};

  EXPECT_EQ(cookies.first("encoded"), "a%2Fb");
  EXPECT_EQ(cookies.first("spaced "), " value");
}

TEST(ResponseCookieTest, SerializesAttributesInStableOrder)
{
  serveza::web::response_cookie cookie;
  cookie.name = "session";
  cookie.value = "token";
  cookie.path = "/app";
  cookie.domain = "example.test";
  cookie.expires = std::chrono::system_clock::from_time_t(784111777);
  cookie.max_age = std::chrono::seconds{3600};
  cookie.same_site = serveza::web::same_site::strict;
  cookie.secure = true;
  cookie.http_only = true;

  EXPECT_EQ(serveza::web::serialize_cookie(cookie),
            "session=token; Path=/app; Domain=example.test; Expires=Sun, 06 Nov 1994 08:49:37 GMT; Max-Age=3600; "
            "SameSite=Strict; Secure; HttpOnly");
}

TEST(ResponseCookieTest, SerializesLaxAndSecureNoneSameSitePolicies)
{
  serveza::web::response_cookie cookie;
  cookie.name = "preference";
  cookie.value = "value";
  cookie.same_site = serveza::web::same_site::lax;
  EXPECT_EQ(serveza::web::serialize_cookie(cookie), "preference=value; SameSite=Lax");

  cookie.same_site = serveza::web::same_site::none;
  cookie.secure = true;
  EXPECT_EQ(serveza::web::serialize_cookie(cookie), "preference=value; SameSite=None; Secure");
}

TEST(ResponseCookieTest, EnforcesCookiePrefixAndSameSiteRules)
{
  serveza::web::response_cookie cookie;
  cookie.name = "__Secure-token";
  cookie.value = "value";
  EXPECT_THROW(serveza::web::serialize_cookie(cookie), std::invalid_argument);
  cookie.secure = true;
  EXPECT_NO_THROW(serveza::web::serialize_cookie(cookie));

  cookie = {};
  cookie.name = "__Host-token";
  cookie.value = "value";
  cookie.secure = true;
  cookie.path = "/";
  EXPECT_NO_THROW(serveza::web::serialize_cookie(cookie));
  cookie.domain = "example.test";
  EXPECT_THROW(serveza::web::serialize_cookie(cookie), std::invalid_argument);

  cookie = {};
  cookie.name = "ordinary";
  cookie.value = "value";
  cookie.same_site = serveza::web::same_site::none;
  EXPECT_THROW(serveza::web::serialize_cookie(cookie), std::invalid_argument);
}

TEST(ResponseCookieTest, RejectsInjectionAndInvalidTokens)
{
  EXPECT_THROW(serveza::web::serialize_cookie({}), std::invalid_argument);
  serveza::web::response_cookie cookie;
  cookie.name = "bad name";
  cookie.value = "value";
  EXPECT_THROW(serveza::web::serialize_cookie(cookie), std::invalid_argument);

  cookie = {};
  cookie.name = "name";
  cookie.value = "bad;value";
  EXPECT_THROW(serveza::web::serialize_cookie(cookie), std::invalid_argument);

  cookie.value = "value";
  cookie.path = "/\r\nInjected: yes";
  EXPECT_THROW(serveza::web::serialize_cookie(cookie), std::invalid_argument);
}

} // namespace
