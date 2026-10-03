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

#include "http_date.h"

#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace serveza::web::details {

namespace {

bool utc_time(std::time_t value, std::tm& result) noexcept
{
#if defined(_WIN32)
  return gmtime_s(&result, &value) == 0;
#else
  return gmtime_r(&value, &result) != nullptr;
#endif
}

std::time_t utc_timestamp(std::tm& value) noexcept
{
#if defined(_WIN32)
  return _mkgmtime64(&value);
#else
  return timegm(&value);
#endif
}

std::optional<std::chrono::system_clock::time_point> parse_http_date(std::string_view value, const char* format)
{
  std::tm broken_down{};
  std::istringstream stream{std::string{value}};
  stream.imbue(std::locale::classic());
  stream >> std::get_time(&broken_down, format);
  if (stream.fail()) return std::nullopt;
  stream >> std::ws;
  if (!stream.eof()) return std::nullopt;
  const auto parsed = broken_down;
  const auto timestamp = utc_timestamp(broken_down);
  if (timestamp == static_cast<std::time_t>(-1)) return std::nullopt;
  if (broken_down.tm_year != parsed.tm_year || broken_down.tm_mon != parsed.tm_mon ||
      broken_down.tm_mday != parsed.tm_mday || broken_down.tm_hour != parsed.tm_hour ||
      broken_down.tm_min != parsed.tm_min || broken_down.tm_sec != parsed.tm_sec)
    return std::nullopt;
  return std::chrono::system_clock::from_time_t(timestamp);
}

} // namespace

std::string format_http_date(std::chrono::system_clock::time_point value)
{
  const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(value);
  const auto timestamp = std::chrono::system_clock::to_time_t(seconds);
  std::tm broken_down{};
  if (!utc_time(timestamp, broken_down)) throw std::out_of_range{"HTTP date is outside the supported range"};

  constexpr std::string_view weekdays[]{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  constexpr std::string_view months[]{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const int year = broken_down.tm_year + 1900;
  if (broken_down.tm_wday < 0 || broken_down.tm_wday >= 7 || broken_down.tm_mon < 0 || broken_down.tm_mon >= 12 ||
      broken_down.tm_mday < 1 || broken_down.tm_mday > 31 || year < 0 || year > 9999 || broken_down.tm_hour < 0 ||
      broken_down.tm_hour > 23 || broken_down.tm_min < 0 || broken_down.tm_min > 59 || broken_down.tm_sec < 0 ||
      broken_down.tm_sec > 60)
    throw std::out_of_range{"HTTP date is outside the supported range"};

  const auto write_two_digits = [](char* output, int number) {
    output[0] = static_cast<char>('0' + number / 10);
    output[1] = static_cast<char>('0' + number % 10);
  };
  const auto write_four_digits = [](char* output, int number) {
    output[0] = static_cast<char>('0' + number / 1000);
    output[1] = static_cast<char>('0' + number / 100 % 10);
    output[2] = static_cast<char>('0' + number / 10 % 10);
    output[3] = static_cast<char>('0' + number % 10);
  };

  std::string result{"---, -- --- ---- --:--:-- GMT"};
  result.replace(0, 3, weekdays[static_cast<std::size_t>(broken_down.tm_wday)]);
  write_two_digits(result.data() + 5, broken_down.tm_mday);
  result.replace(8, 3, months[static_cast<std::size_t>(broken_down.tm_mon)]);
  write_four_digits(result.data() + 12, year);
  write_two_digits(result.data() + 17, broken_down.tm_hour);
  write_two_digits(result.data() + 20, broken_down.tm_min);
  write_two_digits(result.data() + 23, broken_down.tm_sec);
  return result;
}

std::optional<std::chrono::system_clock::time_point> parse_http_date(std::string_view value)
{
  if (auto result = parse_http_date(value, "%a, %d %b %Y %H:%M:%S GMT")) return result;
  if (auto result = parse_http_date(value, "%A, %d-%b-%y %H:%M:%S GMT")) return result;
  return parse_http_date(value, "%a %b %d %H:%M:%S %Y");
}

} // namespace serveza::web::details
