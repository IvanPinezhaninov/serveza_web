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

#include <serveza/web/cookies.h>

#include <charconv>
#include <stdexcept>

#include <boost/beast/http/field.hpp>

#include <serveza/web/context.h>

#include "detail/http_date.h"

namespace serveza::web {

namespace {

bool is_cookie_name_char(unsigned char ch)
{
  if (ch <= 0x20U || ch >= 0x7fU) return false;
  constexpr std::string_view separators{"()<>@,;:\\\"/[]?={}"};
  return separators.find(static_cast<char>(ch)) == std::string_view::npos;
}

bool is_cookie_value_char(unsigned char ch)
{
  return ch == 0x21U || (ch >= 0x23U && ch <= 0x2bU) || (ch >= 0x2dU && ch <= 0x3aU) || (ch >= 0x3cU && ch <= 0x5bU) ||
         (ch >= 0x5dU && ch <= 0x7eU);
}

void validate_attribute(std::string_view value, std::string_view name)
{
  for (const char raw : value) {
    const auto ch = static_cast<unsigned char>(raw);
    if (ch < 0x20U || ch == 0x7fU || ch == ';')
      throw std::invalid_argument{std::string{name} + " contains an invalid character"};
  }
}

} // namespace

request_cookies_view::request_cookies_view() noexcept = default;

request_cookies_view::request_cookies_view(const boost::beast::http::fields& fields) noexcept
  : m_fields{&fields}
{}

std::optional<std::string_view> request_cookies_view::first(std::string_view name) const noexcept
{
  std::optional<std::string_view> result;
  for_each([&](std::string_view key, std::string_view value) {
    if (!result && key == name) result = value;
  });
  return result;
}

std::string serialize_cookie(const response_cookie& cookie)
{
  if (cookie.name.empty()) throw std::invalid_argument{"cookie name must not be empty"};
  for (const char raw : cookie.name) {
    const auto ch = static_cast<unsigned char>(raw);
    if (!is_cookie_name_char(ch)) throw std::invalid_argument{"cookie name contains an invalid character"};
  }
  for (const char raw : cookie.value) {
    const auto ch = static_cast<unsigned char>(raw);
    if (!is_cookie_value_char(ch)) throw std::invalid_argument{"cookie value contains an invalid character"};
  }
  validate_attribute(cookie.path, "cookie path");
  validate_attribute(cookie.domain, "cookie domain");

  if (cookie.same_site == same_site::none && !cookie.secure)
    throw std::invalid_argument{"SameSite=None cookie must be Secure"};
  if (cookie.name.rfind("__Secure-", 0) == 0 && !cookie.secure)
    throw std::invalid_argument{"__Secure- cookie must be Secure"};
  if (cookie.name.rfind("__Host-", 0) == 0 && (!cookie.secure || cookie.path != "/" || !cookie.domain.empty()))
    throw std::invalid_argument{"__Host- cookie must be Secure, use Path=/, and omit Domain"};

  std::string result;
  result.reserve(cookie.name.size() + cookie.value.size() + cookie.path.size() + cookie.domain.size() + 80);
  result += cookie.name;
  result.push_back('=');
  result += cookie.value;
  if (!cookie.path.empty()) result += "; Path=" + cookie.path;
  if (!cookie.domain.empty()) result += "; Domain=" + cookie.domain;
  if (cookie.expires) result += "; Expires=" + details::format_http_date(*cookie.expires);
  if (cookie.max_age) {
    char value[32];
    const auto [end, ec] = std::to_chars(value, value + sizeof(value), cookie.max_age->count());
    if (ec != std::errc{}) throw std::length_error{"cookie Max-Age is too large"};
    result += "; Max-Age=";
    result.append(value, end);
  }
  switch (cookie.same_site) {
  case same_site::strict:
    result += "; SameSite=Strict";
    break;
  case same_site::lax:
    result += "; SameSite=Lax";
    break;
  case same_site::none:
    result += "; SameSite=None";
    break;
  case same_site::unspecified:
    break;
  }
  if (cookie.secure) result += "; Secure";
  if (cookie.http_only) result += "; HttpOnly";
  return result;
}

request_cookies_view request_cookies(const request_context& ctx) noexcept
{
  return request_cookies_view{ctx.request_headers()};
}

void set_cookie(request_context& ctx, const response_cookie& cookie)
{
  ctx.response_headers().insert(boost::beast::http::field::set_cookie, serialize_cookie(cookie));
}

} // namespace serveza::web
