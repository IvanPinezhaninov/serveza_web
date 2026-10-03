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

#ifndef SERVEZA_WEB_COOKIES_H
#define SERVEZA_WEB_COOKIES_H

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include <boost/beast/http/fields.hpp>

#include <serveza/web/export.h>

namespace serveza::web {

class request_context;

/**
 * @brief Non-owning parsed view of all Cookie request headers.
 *
 * Cookie values are returned verbatim and are not percent-decoded. The view
 * must not outlive the request headers from which it was constructed.
 */
class SERVEZA_WEB_API request_cookies_view final {
public:
  /** @brief Creates an empty cookie view. */
  request_cookies_view() noexcept;

  /** @brief Creates a cookie view over @p fields without copying them. */
  explicit request_cookies_view(const boost::beast::http::fields& fields) noexcept;

  /** @brief Returns the first cookie value named @p name, or no value when absent. */
  [[nodiscard]] std::optional<std::string_view> first(std::string_view name) const noexcept;

  /** @brief Calls @p visitor with every cookie name and value in header order. */
  template<typename Visitor>
  void for_each(Visitor&& visitor) const
  {
    if (!m_fields) return;
    for (const auto& field : *m_fields) {
      if (field.name() != boost::beast::http::field::cookie) continue;
      std::string_view remaining{field.value().data(), field.value().size()};
      while (!remaining.empty()) {
        const auto separator = remaining.find(';');
        auto item = remaining.substr(0, separator);
        remaining = separator == std::string_view::npos ? std::string_view{} : remaining.substr(separator + 1);
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t'))
          item.remove_prefix(1);
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t'))
          item.remove_suffix(1);
        const auto equals = item.find('=');
        if (equals == std::string_view::npos || equals == 0) continue;
        visitor(item.substr(0, equals), item.substr(equals + 1));
      }
    }
  }

private:
  const boost::beast::http::fields* m_fields{};
};

/** @brief SameSite attribute emitted for a response cookie. */
enum class same_site {
  /** @brief Omit the SameSite attribute. */
  unspecified,

  /** @brief Emit @c SameSite=Strict. */
  strict,

  /** @brief Emit @c SameSite=Lax. */
  lax,

  /** @brief Emit @c SameSite=None; this requires @ref response_cookie::secure. */
  none,
};

/** @brief Attributes used to construct one Set-Cookie response header. */
struct response_cookie {
  /** @brief Cookie name; it must be a non-empty RFC token. */
  std::string name;

  /** @brief Cookie value, stored without quoting or encoding. */
  std::string value;

  /** @brief Optional Path attribute. */
  std::string path;

  /** @brief Optional Domain attribute. */
  std::string domain;

  /** @brief Optional absolute expiry time emitted as an IMF-fixdate Expires attribute. */
  std::optional<std::chrono::system_clock::time_point> expires;

  /** @brief Optional Max-Age attribute. */
  std::optional<std::chrono::seconds> max_age;

  /** @brief SameSite policy to emit. */
  web::same_site same_site{web::same_site::unspecified};

  /** @brief Whether to emit the Secure attribute. */
  bool secure{};

  /** @brief Whether to emit the HttpOnly attribute. */
  bool http_only{};
};

/**
 * @brief Serializes @p cookie as a Set-Cookie field value.
 * @throws std::invalid_argument if the cookie or its attributes are invalid.
 */
SERVEZA_WEB_API std::string serialize_cookie(const response_cookie& cookie);

/** @brief Returns a non-owning view of the cookies in @p ctx. */
[[nodiscard]] SERVEZA_WEB_API request_cookies_view request_cookies(const request_context& ctx) noexcept;

/**
 * @brief Appends @p cookie to the response headers of @p ctx.
 * @throws std::invalid_argument if the cookie is invalid.
 */
SERVEZA_WEB_API void set_cookie(request_context& ctx, const response_cookie& cookie);

} // namespace serveza::web

#endif // SERVEZA_WEB_COOKIES_H
