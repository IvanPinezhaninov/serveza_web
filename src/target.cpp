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

#include <serveza/web/target.h>

#include <cctype>
#include <stdexcept>

#include "detail/core.h"

namespace serveza::web {

query_params_view::query_params_view(std::string_view encoded) noexcept
  : m_encoded{encoded}
{}

std::string_view query_params_view::encoded() const noexcept
{
  return m_encoded;
}

std::optional<std::string_view> query_params_view::first(std::string_view name) const noexcept
{
  std::optional<std::string_view> result;
  for_each([&](std::string_view key, std::string_view value) {
    if (!result && key == name) result = value;
  });
  return result;
}

std::string percent_decode(std::string_view value)
{
  auto hex_value = [](char ch) -> unsigned char {
    if (ch >= '0' && ch <= '9') return static_cast<unsigned char>(ch - '0');
    if (ch >= 'a' && ch <= 'f') return static_cast<unsigned char>(ch - 'a' + 10);
    if (ch >= 'A' && ch <= 'F') return static_cast<unsigned char>(ch - 'A' + 10);
    throw std::invalid_argument{"malformed percent escape"};
  };

  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] != '%') {
      decoded.push_back(value[i]);
      continue;
    }
    if (i + 2 >= value.size()) throw std::invalid_argument{"malformed percent escape"};
    const auto high = hex_value(value[i + 1]);
    const auto low = hex_value(value[i + 2]);
    decoded.push_back(static_cast<char>((high << 4U) | low));
    i += 2;
  }
  return decoded;
}

namespace details {

target_view parse_target(std::string_view raw)
{
  if (raw.empty()) raw = "/";
  if (raw.find('#') != std::string_view::npos)
    throw std::invalid_argument{"HTTP request target must not contain a fragment"};

  const auto question = raw.find('?');
  const auto path = raw.substr(0, question);
  const auto query = question == std::string_view::npos ? std::string_view{} : raw.substr(question + 1);

  if (path != "*" && (path.empty() || path.front() != '/'))
    throw std::invalid_argument{"unsupported HTTP request-target form"};

  auto validate_escapes = [](std::string_view value) {
    for (std::size_t i = 0; i < value.size(); ++i) {
      if (value[i] != '%') continue;
      if (i + 2 >= value.size() || std::isxdigit(static_cast<unsigned char>(value[i + 1])) == 0 ||
          std::isxdigit(static_cast<unsigned char>(value[i + 2])) == 0)
        throw std::invalid_argument{"malformed percent escape"};
      i += 2;
    }
  };

  validate_escapes(path);
  validate_escapes(query);
  return {raw, path, query_params_view{query}};
}

} // namespace details
} // namespace serveza::web
