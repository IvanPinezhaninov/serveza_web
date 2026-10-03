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

#ifndef SERVEZA_WEB_ROUTE_PARAMS_H
#define SERVEZA_WEB_ROUTE_PARAMS_H

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include <serveza/web/export.h>

namespace serveza::web {

/// @cond INTERNAL
namespace details {
struct route_params_access;
}
/// @endcond

/** @brief Non-owning name/value pair captured while matching a route. */
struct route_param {
  /** @brief Parameter name stored by the compiled route pattern. */
  std::string_view name;

  /** @brief Encoded value viewed directly in the current request path. */
  std::string_view value;
};

/**
 * @brief Fixed-capacity view of parameters captured for the current route.
 *
 * Names and values remain valid only while the current handler invocation is
 * active. Copy values that must outlive the request.
 */
class SERVEZA_WEB_API route_params final {
public:
  /** @brief Maximum number of parameters that one matched route can expose. */
  static constexpr std::size_t capacity = 16;

  /** @brief Read-only iterator over captured parameters. */
  using const_iterator = const route_param*;

  /** @brief Returns the number of captured parameters. */
  [[nodiscard]] std::size_t size() const noexcept;

  /** @brief Returns whether no parameters were captured. */
  [[nodiscard]] bool empty() const noexcept;

  /** @brief Returns an iterator to the first captured parameter. */
  [[nodiscard]] const_iterator begin() const noexcept;

  /** @brief Returns an iterator past the last captured parameter. */
  [[nodiscard]] const_iterator end() const noexcept;

  /** @brief Returns the first value named @p name, or no value when it is absent. */
  [[nodiscard]] std::optional<std::string_view> find(std::string_view name) const noexcept;

  /** @brief Returns the value named @p name, throwing @c std::out_of_range when absent. */
  [[nodiscard]] std::string_view at(std::string_view name) const;

private:
  friend struct details::route_params_access;

  std::array<route_param, capacity> m_values{};
  std::size_t m_size{};
};

} // namespace serveza::web

#endif // SERVEZA_WEB_ROUTE_PARAMS_H
