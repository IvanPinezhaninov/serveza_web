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

#include <serveza/web/middleware/cors.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <boost/beast/http.hpp>

#include <serveza/web/context.h>
#include <serveza/web/middleware.h>

#include "header_utils.h"

namespace serveza::web::middleware {

namespace {

namespace http = boost::beast::http;

std::string_view trim_ows(std::string_view value) noexcept
{
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
    value.remove_prefix(1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
    value.remove_suffix(1);
  return value;
}

bool is_token_character(unsigned char value) noexcept
{
  if ((value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z')) return true;
  constexpr std::string_view punctuation{"!#$%&'*+-.^_`|~"};
  return punctuation.find(static_cast<char>(value)) != std::string_view::npos;
}

bool is_token(std::string_view value) noexcept
{
  return !value.empty() && std::all_of(value.begin(), value.end(),
                                       [](char raw) { return is_token_character(static_cast<unsigned char>(raw)); });
}

bool ascii_iequals(std::string_view left, std::string_view right) noexcept
{
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    auto lhs = static_cast<unsigned char>(left[index]);
    auto rhs = static_cast<unsigned char>(right[index]);
    if (lhs >= 'A' && lhs <= 'Z') lhs = static_cast<unsigned char>(lhs + ('a' - 'A'));
    if (rhs >= 'A' && rhs <= 'Z') rhs = static_cast<unsigned char>(rhs + ('a' - 'A'));
    if (lhs != rhs) return false;
  }
  return true;
}

std::vector<std::string> parse_token_list(std::string_view list, std::string_view option_name, bool may_be_empty)
{
  std::vector<std::string> result;
  while (!list.empty()) {
    const auto comma = list.find(',');
    const auto item = trim_ows(list.substr(0, comma));
    if (!is_token(item)) throw std::invalid_argument{std::string{option_name} + " contains an invalid token"};
    result.emplace_back(item);
    if (comma == std::string_view::npos) break;
    list.remove_prefix(comma + 1);
    if (list.empty()) throw std::invalid_argument{std::string{option_name} + " contains an empty token"};
  }
  if (!may_be_empty && result.empty()) throw std::invalid_argument{std::string{option_name} + " must not be empty"};
  return result;
}

void validate_field_value(std::string_view value, std::string_view option_name)
{
  for (const char raw : value) {
    const auto character = static_cast<unsigned char>(raw);
    if ((character < 0x20U && character != '\t') || character == 0x7fU)
      throw std::invalid_argument{std::string{option_name} + " contains an invalid character"};
  }
}

} // namespace

cors::cors(cors_options options)
  : m_options{std::move(options)}
  , m_allowed_methods{parse_token_list(m_options.allowed_methods, "CORS allowed methods", false)}
{
  if (m_options.max_age < std::chrono::seconds::zero())
    throw std::invalid_argument{"CORS maximum age must not be negative"};
  for (const auto& origin : m_options.allowed_origins) {
    validate_field_value(origin, "CORS allowed origin");
    if (origin.empty()) throw std::invalid_argument{"CORS allowed origin must not be empty"};
  }
  validate_field_value(m_options.exposed_headers, "CORS exposed headers");
  static_cast<void>(parse_token_list(m_options.exposed_headers, "CORS exposed headers", true));

  const auto allowed_headers = trim_ows(m_options.allowed_headers);
  m_allow_any_header = allowed_headers == "*";
  if (!m_allow_any_header)
    m_allowed_headers = parse_token_list(m_options.allowed_headers, "CORS allowed headers", true);
}

bool cors::allows(std::string_view origin) const noexcept
{
  if (m_options.allow_any_origin) return true;
  return std::any_of(m_options.allowed_origins.begin(), m_options.allowed_origins.end(),
                     [&](const std::string& allowed) { return allowed == origin; });
}

bool cors::allows_method(std::string_view method) const noexcept
{
  method = trim_ows(method);
  return std::any_of(m_allowed_methods.begin(), m_allowed_methods.end(),
                     [&](const std::string& allowed) { return allowed == method; });
}

bool cors::allows_headers(std::string_view headers) const noexcept
{
  while (!headers.empty()) {
    const auto comma = headers.find(',');
    const auto header = trim_ows(headers.substr(0, comma));
    if (!is_token(header)) return false;
    if (!m_allow_any_header && std::none_of(m_allowed_headers.begin(), m_allowed_headers.end(),
                                            [&](const std::string& allowed) { return ascii_iequals(allowed, header); }))
      return false;
    if (comma == std::string_view::npos) break;
    headers.remove_prefix(comma + 1);
    if (headers.empty()) return false;
  }
  return true;
}

void cors::operator()(request_context& ctx, continuation& next, completion_handler handler) const
{
  const auto origin = details::header_value(ctx.request_headers(), "Origin");
  if (!origin || !allows(*origin)) {
    next(std::move(handler));
    return;
  }

  auto& headers = ctx.response_headers();
  const auto allow_origin =
      m_options.allow_any_origin && !m_options.allow_credentials ? std::string_view{"*"} : *origin;
  details::set_header(headers, "Access-Control-Allow-Origin", allow_origin);
  headers.insert(http::field::vary, "Origin");
  if (m_options.allow_credentials) details::set_header(headers, "Access-Control-Allow-Credentials", "true");
  if (!m_options.exposed_headers.empty())
    details::set_header(headers, "Access-Control-Expose-Headers", m_options.exposed_headers);

  const auto requested_method = details::header_value(ctx.request_headers(), "Access-Control-Request-Method");
  if (ctx.method() == http::verb::options && requested_method) {
    headers.insert(http::field::vary, "Access-Control-Request-Method");
    const auto requested_headers = details::header_value(ctx.request_headers(), "Access-Control-Request-Headers");
    if (requested_headers) headers.insert(http::field::vary, "Access-Control-Request-Headers");
    if (!allows_method(*requested_method) || (requested_headers && !allows_headers(*requested_headers))) {
      ctx.async_send_status(http::status::forbidden, std::move(handler));
      return;
    }
    details::set_header(headers, "Access-Control-Allow-Methods", m_options.allowed_methods);
    if (requested_headers && m_allow_any_header && m_options.allow_credentials)
      details::set_header(headers, "Access-Control-Allow-Headers", *requested_headers);
    else if (!m_options.allowed_headers.empty())
      details::set_header(headers, "Access-Control-Allow-Headers", m_options.allowed_headers);
    if (m_options.max_age.count() > 0) {
      const auto value = std::to_string(m_options.max_age.count());
      details::set_header(headers, "Access-Control-Max-Age", value);
    }
    ctx.async_send_status(http::status::no_content, std::move(handler));
    return;
  }

  next(std::move(handler));
}

} // namespace serveza::web::middleware
