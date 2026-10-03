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

#include "detail/route_pattern.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace serveza::web::details {

namespace {

bool valid_name(std::string_view value)
{
  if (value.empty()) return false;
  for (const char raw : value) {
    const auto ch = static_cast<unsigned char>(raw);
    if (std::isalnum(ch) == 0 && ch != '_') return false;
  }
  return true;
}

bool all_decimal(std::string_view value)
{
  return !value.empty() && std::all_of(value.begin(), value.end(), [](const char raw) {
    const auto ch = static_cast<unsigned char>(raw);
    return ch >= '0' && ch <= '9';
  });
}

bool all_hexadecimal(std::string_view value)
{
  return !value.empty() && std::all_of(value.begin(), value.end(), [](const char raw) {
    const auto ch = static_cast<unsigned char>(raw);
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
  });
}

bool uuid(std::string_view value)
{
  if (value.size() != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-') return false;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) continue;
    if (!all_hexadecimal(value.substr(i, 1))) return false;
  }
  return true;
}

bool parse_enumeration(std::string_view expression, std::vector<std::string>& alternatives)
{
  if (expression.find('|') == std::string_view::npos) return false;
  while (!expression.empty()) {
    const auto separator = expression.find('|');
    const auto item = expression.substr(0, separator);
    if (!valid_name(item)) return false;
    alternatives.emplace_back(item);
    expression = separator == std::string_view::npos ? std::string_view{} : expression.substr(separator + 1);
  }
  return alternatives.size() > 1;
}

std::size_t constraint_end(std::string_view value, std::size_t open)
{
  std::size_t depth = 1;
  bool escaped = false;
  bool character_class = false;
  for (std::size_t i = open + 1; i < value.size(); ++i) {
    const char ch = value[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '[') {
      character_class = true;
      continue;
    }
    if (ch == ']' && character_class) {
      character_class = false;
      continue;
    }
    if (character_class) continue;
    if (ch == '(') {
      ++depth;
    } else if (ch == ')' && --depth == 0) {
      return i;
    }
  }
  return std::string_view::npos;
}

std::string normalize_pattern(std::string pattern)
{
  if (pattern.empty()) return "/";
  if (pattern.front() != '/') pattern.insert(pattern.begin(), '/');
  return pattern;
}

std::size_t optional_group_end(std::string_view pattern, std::size_t open)
{
  std::size_t constraint_depth{};
  bool escaped{};
  bool character_class{};
  for (std::size_t i = open + 1; i < pattern.size(); ++i) {
    const char ch = pattern[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\' && constraint_depth != 0) {
      escaped = true;
      continue;
    }
    if (ch == '[' && constraint_depth != 0) {
      character_class = true;
      continue;
    }
    if (ch == ']' && character_class) {
      character_class = false;
      continue;
    }
    if (character_class) continue;
    if (ch == '(') {
      ++constraint_depth;
      continue;
    }
    if (ch == ')' && constraint_depth != 0) {
      --constraint_depth;
      continue;
    }
    if (constraint_depth != 0) continue;
    if (ch == '{') throw std::invalid_argument{"nested route optional groups are not supported"};
    if (ch == '}') return i;
  }
  return std::string_view::npos;
}

std::string expand_optional_segment_groups(std::string_view pattern)
{
  std::string result;
  result.reserve(pattern.size());
  std::size_t constraint_depth{};
  bool escaped{};
  bool character_class{};

  for (std::size_t i = 0; i < pattern.size(); ++i) {
    const char ch = pattern[i];
    if (escaped) {
      result.push_back(ch);
      escaped = false;
      continue;
    }
    if (ch == '\\' && constraint_depth != 0) {
      result.push_back(ch);
      escaped = true;
      continue;
    }
    if (ch == '[' && constraint_depth != 0) {
      character_class = true;
      result.push_back(ch);
      continue;
    }
    if (ch == ']' && character_class) {
      character_class = false;
      result.push_back(ch);
      continue;
    }
    if (!character_class && ch == '(') {
      ++constraint_depth;
      result.push_back(ch);
      continue;
    }
    if (!character_class && ch == ')' && constraint_depth != 0) {
      --constraint_depth;
      result.push_back(ch);
      continue;
    }
    if (constraint_depth != 0 || ch != '{') {
      if (constraint_depth == 0 && ch == '}') throw std::invalid_argument{"unmatched route optional-group terminator"};
      result.push_back(ch);
      continue;
    }

    const auto close = optional_group_end(pattern, i);
    if (close == std::string_view::npos) throw std::invalid_argument{"unterminated route optional group"};
    const auto group = pattern.substr(i + 1, close - i - 1);
    if (group.size() < 2 || group.front() != '/' || group.find('/', 1) != std::string_view::npos)
      throw std::invalid_argument{"optional route group must contain one complete segment"};
    result.append(group);
    result.push_back('?');
    i = close;
  }

  if (constraint_depth != 0) return result;
  return result;
}

} // namespace

compiled_pattern::compiled_pattern(std::string pattern, mode match_mode, const settings& config)
  : m_source{normalize_pattern(std::move(pattern))}
  , m_mode{match_mode}
  , m_trailing_slash{config.trailing_slash}
  , m_regex_segment_limit{config.regex_segment_limit}
  , m_pattern_has_trailing_slash{m_source.size() > 1 && m_source.back() == '/'}
{
  const auto expanded = expand_optional_segment_groups(m_source);
  std::string_view remaining{expanded};
  remaining.remove_prefix(1);
  if (m_pattern_has_trailing_slash) remaining.remove_suffix(1);
  if (remaining.empty()) return;

  std::unordered_set<std::string> parameter_names;
  std::size_t parameter_count = 0;

  while (!remaining.empty()) {
    const auto slash = remaining.find('/');
    const auto raw_segment = remaining.substr(0, slash);
    remaining = slash == std::string_view::npos ? std::string_view{} : remaining.substr(slash + 1);
    if (raw_segment.empty()) throw std::invalid_argument{"route pattern contains an empty segment"};

    segment value;
    if (raw_segment.front() == '*') {
      if (!remaining.empty()) throw std::invalid_argument{"route wildcard must be the final segment"};
      value.kind = segment_kind::wildcard;
      value.wildcard_name = std::string{raw_segment.substr(1)};
      if (!valid_name(value.wildcard_name)) throw std::invalid_argument{"invalid route parameter name"};
      if (!parameter_names.emplace(value.wildcard_name).second)
        throw std::invalid_argument{"duplicate route parameter name"};
      if (++parameter_count > route_params::capacity) throw std::invalid_argument{"too many route parameters"};
    } else {
      std::size_t position = 0;
      while (position < raw_segment.size()) {
        if (raw_segment[position] != ':') {
          const auto start = position;
          while (position < raw_segment.size() && raw_segment[position] != ':') {
            const char ch = raw_segment[position];
            if (ch == '*' || ch == '?' || ch == '{' || ch == '}')
              throw std::invalid_argument{"reserved character in route literal"};
            ++position;
          }
          component literal;
          literal.value = std::string{raw_segment.substr(start, position - start)};
          value.components.push_back(std::move(literal));
          continue;
        }

        ++position;
        const auto name_start = position;
        while (position < raw_segment.size()) {
          const auto ch = static_cast<unsigned char>(raw_segment[position]);
          if (std::isalnum(ch) == 0 && ch != '_') break;
          ++position;
        }
        const auto name = raw_segment.substr(name_start, position - name_start);
        if (!valid_name(name)) throw std::invalid_argument{"invalid route parameter name"};
        if (!value.components.empty() && value.components.back().kind == component_kind::parameter)
          throw std::invalid_argument{"adjacent route parameters are ambiguous"};

        component parameter;
        parameter.kind = component_kind::parameter;
        parameter.value = std::string{name};
        if (position < raw_segment.size() && raw_segment[position] == '(') {
          const auto close = constraint_end(raw_segment, position);
          if (close == std::string_view::npos) throw std::invalid_argument{"invalid constrained route parameter"};
          const auto expression = raw_segment.substr(position + 1, close - position - 1);
          if (expression.empty()) throw std::invalid_argument{"empty route parameter constraint"};
          if (expression == "[0-9]+" || expression == "\\d+") {
            parameter.constraint_type = constraint_kind::decimal;
          } else if (expression == "[0-9a-fA-F]+" || expression == "[0-9A-Fa-f]+") {
            parameter.constraint_type = constraint_kind::hexadecimal;
          } else if (expression == "uuid") {
            parameter.constraint_type = constraint_kind::uuid;
          } else if (parse_enumeration(expression, parameter.alternatives)) {
            parameter.constraint_type = constraint_kind::enumeration;
          } else {
            parameter.constraint_type = constraint_kind::regular_expression;
            try {
              parameter.constraint.emplace(std::string{expression},
                                           std::regex_constants::ECMAScript | std::regex_constants::optimize);
            } catch (const std::regex_error&) {
              throw std::invalid_argument{"invalid route parameter regular expression"};
            }
          }
          position = close + 1;
        }

        if (position < raw_segment.size() && raw_segment[position] == '?') {
          if (!value.components.empty() || position + 1 != raw_segment.size())
            throw std::invalid_argument{"only a complete route segment may be optional"};
          value.optional = true;
          ++position;
        }

        if (!parameter_names.emplace(parameter.value).second)
          throw std::invalid_argument{"duplicate route parameter name"};
        if (++parameter_count > route_params::capacity) throw std::invalid_argument{"too many route parameters"};
        value.components.push_back(std::move(parameter));
      }
    }

    m_segments.push_back(std::move(value));
  }
}

bool compiled_pattern::match(std::string_view path, route_params& params) const
{
  route_params_access::clear(params);
  if (path.empty() || path.front() != '/') return false;

  const bool request_has_trailing_slash = path.size() > 1 && path.back() == '/';
  if (m_trailing_slash == trailing_slash_policy::strict && request_has_trailing_slash != m_pattern_has_trailing_slash)
    return false;
  if (request_has_trailing_slash) path.remove_suffix(1);

  path_segments input{};
  std::size_t input_size = 0;
  std::string_view remaining = path.substr(1);
  while (!remaining.empty()) {
    if (input_size == input.size()) return false;
    const auto slash = remaining.find('/');
    const auto value = remaining.substr(0, slash);
    if (value.empty()) return false;
    input[input_size++] = value;
    remaining = slash == std::string_view::npos ? std::string_view{} : remaining.substr(slash + 1);
  }

  return match_from(input, input_size, 0, 0, params);
}

std::optional<std::string_view> compiled_pattern::first_literal() const noexcept
{
  return indexable_literal(0);
}

std::size_t compiled_pattern::indexable_segment_count() const noexcept
{
  std::size_t result{};
  for (const auto& value : m_segments) {
    if (value.kind == segment_kind::wildcard || value.optional) break;
    ++result;
  }
  return result;
}

std::optional<std::string_view> compiled_pattern::indexable_literal(std::size_t index) const noexcept
{
  if (index >= indexable_segment_count()) return std::nullopt;
  const auto& components = m_segments[index].components;
  if (components.size() != 1 || components.front().kind != component_kind::literal) return std::nullopt;
  return components.front().value;
}

bool compiled_pattern::matches_constraint(const component& pattern, std::string_view value) const
{
  switch (pattern.constraint_type) {
  case constraint_kind::none:
    return true;
  case constraint_kind::decimal:
    return all_decimal(value);
  case constraint_kind::hexadecimal:
    return all_hexadecimal(value);
  case constraint_kind::uuid:
    return uuid(value);
  case constraint_kind::enumeration:
    return std::any_of(pattern.alternatives.begin(), pattern.alternatives.end(),
                       [value](const auto& alternative) { return alternative == value; });
  case constraint_kind::regular_expression:
    return value.size() <= m_regex_segment_limit && std::regex_match(value.begin(), value.end(), *pattern.constraint);
  }
  return false;
}

bool compiled_pattern::match_components(const segment& pattern, std::size_t component_index, std::string_view value,
                                        std::size_t value_index, route_params& params) const
{
  if (component_index == pattern.components.size()) return value_index == value.size();

  const auto& pattern_component = pattern.components[component_index];
  if (pattern_component.kind == component_kind::literal) {
    if (value.substr(value_index, pattern_component.value.size()) != pattern_component.value) return false;
    return match_components(pattern, component_index + 1, value, value_index + pattern_component.value.size(), params);
  }

  const auto saved_size = params.size();
  if (component_index + 1 == pattern.components.size()) {
    const auto captured = value.substr(value_index);
    return !captured.empty() && matches_constraint(pattern_component, captured) &&
           route_params_access::push(params, pattern_component.value, captured);
  }

  const auto& delimiter = pattern.components[component_index + 1].value;
  auto delimiter_position = value.find(delimiter, value_index + 1);
  while (delimiter_position != std::string_view::npos) {
    const auto captured = value.substr(value_index, delimiter_position - value_index);
    if (matches_constraint(pattern_component, captured) &&
        route_params_access::push(params, pattern_component.value, captured) &&
        match_components(pattern, component_index + 1, value, delimiter_position, params))
      return true;
    route_params_access::resize(params, saved_size);
    delimiter_position = value.find(delimiter, delimiter_position + 1);
  }
  return false;
}

bool compiled_pattern::match_from(const path_segments& input, std::size_t input_size, std::size_t pattern_index,
                                  std::size_t input_index, route_params& params) const
{
  if (pattern_index == m_segments.size()) return m_mode == mode::prefix || input_index == input_size;

  const auto& pattern_segment = m_segments[pattern_index];
  if (pattern_segment.kind == segment_kind::wildcard) {
    if (input_index == input_size) return false;
    const auto* first = input[input_index].data();
    const auto& last_segment = input[input_size - 1];
    const auto* last = last_segment.data() + last_segment.size();
    return route_params_access::push(params, pattern_segment.wildcard_name,
                                     std::string_view{first, static_cast<std::size_t>(last - first)});
  }

  const auto saved_size = params.size();
  if (input_index < input_size) {
    const auto candidate = input[input_index];
    const bool segment_matches = match_components(pattern_segment, 0, candidate, 0, params);

    if (segment_matches && match_from(input, input_size, pattern_index + 1, input_index + 1, params)) return true;
    route_params_access::resize(params, saved_size);
  }

  if (pattern_segment.optional && match_from(input, input_size, pattern_index + 1, input_index, params)) return true;

  route_params_access::resize(params, saved_size);
  return false;
}

} // namespace serveza::web::details
