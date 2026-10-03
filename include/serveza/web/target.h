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

#ifndef SERVEZA_WEB_TARGET_H
#define SERVEZA_WEB_TARGET_H

#include <optional>
#include <string>
#include <string_view>

#include <serveza/web/export.h>

namespace serveza::web {

/**
 * @brief Non-owning view of an encoded URL query string.
 *
 * Keys and values are not percent-decoded. Their views remain valid only as
 * long as the underlying request target remains valid.
 */
class SERVEZA_WEB_API query_params_view final {
public:
  /** @brief Creates an empty query view. */
  query_params_view() = default;

  /** @brief Creates a view over @p encoded without copying it. */
  explicit query_params_view(std::string_view encoded) noexcept;

  /** @brief Returns the complete encoded query without the leading question mark. */
  [[nodiscard]] std::string_view encoded() const noexcept;

  /** @brief Returns the first encoded value named @p name, or no value when absent. */
  [[nodiscard]] std::optional<std::string_view> first(std::string_view name) const noexcept;

  /** @brief Calls @p visitor with each encoded key and value in source order. */
  template<typename Visitor>
  void for_each(Visitor&& visitor) const
  {
    std::string_view remaining = m_encoded;
    while (!remaining.empty()) {
      const auto separator = remaining.find('&');
      const auto item = remaining.substr(0, separator);
      remaining = separator == std::string_view::npos ? std::string_view{} : remaining.substr(separator + 1);
      if (item.empty()) continue;
      const auto equals = item.find('=');
      const auto key = item.substr(0, equals);
      const auto value = equals == std::string_view::npos ? std::string_view{} : item.substr(equals + 1);
      visitor(key, value);
    }
  }

private:
  std::string_view m_encoded;
};

/** @brief Non-owning decomposition of an HTTP request target. */
struct target_view {
  /** @brief Complete request target, including the encoded query when present. */
  std::string_view raw;

  /** @brief Encoded path component without the query string. */
  std::string_view path;

  /** @brief View of the encoded query component. */
  query_params_view query;
};

/**
 * @brief Percent-decodes @p value.
 * @throws std::invalid_argument if a percent escape is malformed.
 */
SERVEZA_WEB_API std::string percent_decode(std::string_view value);

} // namespace serveza::web

#endif // SERVEZA_WEB_TARGET_H
