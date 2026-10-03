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

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string_view>

#include <serveza/web/target.h>

#include "detail/core.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
  if (size > 16384U) return 0;

  const std::string_view input{reinterpret_cast<const char*>(data), size};
  try {
    const auto target = serveza::web::details::parse_target(input);
    (void)target.query.encoded();
    (void)target.query.first("");
    (void)target.query.first("id");
    target.query.for_each([](std::string_view name, std::string_view value) {
      (void)serveza::web::percent_decode(name);
      (void)serveza::web::percent_decode(value);
    });
    (void)serveza::web::percent_decode(target.path);
  } catch (const std::exception&) {}
  return 0;
}
