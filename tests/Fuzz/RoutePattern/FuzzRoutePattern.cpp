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

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

#include <serveza/web/route_params.h>
#include <serveza/web/settings.h>

#include "detail/route_pattern.h"

namespace {

using Pattern = serveza::web::details::compiled_pattern;

const std::array<Pattern, 8>& trustedPatterns()
{
  static const std::array<Pattern, 8> patterns{
      Pattern{"/", Pattern::mode::exact, {}},
      Pattern{"/api", Pattern::mode::prefix, {}},
      Pattern{"/users/:id", Pattern::mode::exact, {}},
      Pattern{"/users/:id([0-9]+)", Pattern::mode::exact, {}},
      Pattern{"/files/:name.:ext", Pattern::mode::exact, {}},
      Pattern{"/items{/:id([a-z]{2}[0-9]{2})}", Pattern::mode::exact, {}},
      Pattern{"/:kind((foo|bar))-:id", Pattern::mode::exact, {}},
      Pattern{"/assets/*path", Pattern::mode::prefix, {}},
  };
  return patterns;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
  if (size > 4096U) return 0;

  const std::string_view input{reinterpret_cast<const char*>(data), size};
  const auto separator = input.find('\n');
  const auto patternText = input.substr(0, separator);
  const auto path = separator == std::string_view::npos ? input : input.substr(separator + 1);

  serveza::web::settings settings;
  settings.regex_segment_limit = 256U;
  try {
    Pattern exact{std::string{patternText}, Pattern::mode::exact, settings};
    (void)exact.first_literal();
    (void)exact.indexable_segment_count();

    Pattern prefix{std::string{patternText}, Pattern::mode::prefix, settings};
    (void)prefix.first_literal();
    (void)prefix.indexable_segment_count();
  } catch (const std::exception&) {}

  // Route patterns are trusted application configuration. Keep their parser fuzzing
  // separate from matching request paths, which are the remotely controlled input.
  const auto& patterns = trustedPatterns();
  const auto index = size == 0 ? 0U : static_cast<std::size_t>(data[0]) % patterns.size();
  serveza::web::route_params params;
  (void)patterns[index].match(path, params);
  return 0;
}
