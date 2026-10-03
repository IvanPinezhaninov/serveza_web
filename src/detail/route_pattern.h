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

#ifndef SERVEZA_WEB_DETAIL_ROUTE_PATTERN_H
#define SERVEZA_WEB_DETAIL_ROUTE_PATTERN_H

#include <array>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <serveza/web/export.h>
#include <serveza/web/route_params.h>
#include <serveza/web/settings.h>

namespace serveza::web::details {

struct route_params_access final {
  static void clear(route_params& params) noexcept
  {
    params.m_size = 0;
  }

  static void resize(route_params& params, std::size_t size) noexcept
  {
    params.m_size = size;
  }

  static bool push(route_params& params, std::string_view name, std::string_view value) noexcept
  {
    if (params.m_size == route_params::capacity) return false;
    params.m_values[params.m_size++] = {name, value};
    return true;
  }
};

class SERVEZA_WEB_API compiled_pattern final {
public:
  enum class mode { exact, prefix };

  compiled_pattern(std::string pattern, mode match_mode, const settings& config);

  [[nodiscard]] bool match(std::string_view path, route_params& params) const;
  [[nodiscard]] std::string_view source() const noexcept
  {
    return m_source;
  }

  [[nodiscard]] std::optional<std::string_view> first_literal() const noexcept;

  [[nodiscard]] std::size_t indexable_segment_count() const noexcept;

  [[nodiscard]] std::optional<std::string_view> indexable_literal(std::size_t index) const noexcept;

private:
  enum class segment_kind { components, wildcard };
  enum class component_kind { literal, parameter };
  enum class constraint_kind { none, decimal, hexadecimal, uuid, enumeration, regular_expression };

  struct component {
    component_kind kind{component_kind::literal};
    std::string value;
    constraint_kind constraint_type{constraint_kind::none};
    std::vector<std::string> alternatives;
    std::optional<std::regex> constraint;
  };

  struct segment {
    segment_kind kind{segment_kind::components};
    std::string wildcard_name;
    bool optional{};
    std::vector<component> components;
  };

  [[nodiscard]] bool matches_constraint(const component& pattern, std::string_view value) const;

  [[nodiscard]] bool match_components(const segment& pattern, std::size_t component_index, std::string_view value,
                                      std::size_t value_index, route_params& params) const;

  using path_segments = std::array<std::string_view, settings::maximum_path_segments>;

  [[nodiscard]] bool match_from(const path_segments& input, std::size_t input_size, std::size_t pattern_index,
                                std::size_t input_index, route_params& params) const;

  std::string m_source;
  mode m_mode;
  trailing_slash_policy m_trailing_slash;
  std::size_t m_regex_segment_limit;
  bool m_pattern_has_trailing_slash{};
  std::vector<segment> m_segments;
};

} // namespace serveza::web::details

#endif // SERVEZA_WEB_DETAIL_ROUTE_PATTERN_H
