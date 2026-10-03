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
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>

#include <serveza/web/cookies.h>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
  if (size > 16384U) return 0;

  const std::string input{reinterpret_cast<const char*>(data), size};
  try {
    boost::beast::http::fields fields;
    fields.insert(boost::beast::http::field::cookie, input);
    const auto split = input.find('\n');
    if (split != std::string::npos)
      fields.insert(boost::beast::http::field::cookie, std::string_view{input}.substr(split + 1));
    const serveza::web::request_cookies_view cookies{fields};
    (void)cookies.first("");
    (void)cookies.first("session");
    cookies.for_each([](std::string_view, std::string_view) {});
  } catch (const std::exception&) {}

  serveza::web::response_cookie cookie;
  const auto separator = input.find('\n');
  cookie.name = input.substr(0, separator);
  if (separator != std::string::npos) cookie.value = input.substr(separator + 1);
  cookie.path = size % 2U == 0U ? "/" : std::string{};
  cookie.domain = size % 7U == 0U ? "example.test" : std::string{};
  if (size % 11U == 0U)
    cookie.expires = std::chrono::system_clock::time_point{std::chrono::seconds{static_cast<long long>(size)}};
  if (size % 13U == 0U) cookie.max_age = std::chrono::seconds{static_cast<long long>(size) - 32LL};
  cookie.secure = size % 3U == 0U;
  cookie.http_only = size % 5U == 0U;
  cookie.same_site = static_cast<serveza::web::same_site>(size % 4U);
  try {
    (void)serveza::web::serialize_cookie(cookie);
  } catch (const std::exception&) {}
  return 0;
}
