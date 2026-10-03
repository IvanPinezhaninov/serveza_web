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

#include <serveza/web/middleware/request_id.h>

#include <stdexcept>
#include <string_view>
#include <utility>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <serveza/web/context.h>
#include <serveza/web/middleware.h>

#include "header_utils.h"

namespace serveza::web::middleware {

namespace {

bool valid_request_id(std::string_view value, std::size_t max_length)
{
  if (value.empty() || value.size() > max_length) return false;
  for (const char raw : value) {
    const auto ch = static_cast<unsigned char>(raw);
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
        ch == '.')
      continue;
    return false;
  }
  return true;
}

} // namespace

request_id::request_id(request_id_options options)
  : m_options{std::move(options)}
{
  if (m_options.header.empty()) throw std::invalid_argument{"request ID header must not be empty"};
}

void request_id::operator()(request_context& ctx, continuation& next, completion_handler handler) const
{
  std::string value;
  if (m_options.trust_incoming) {
    const auto incoming = details::header_value(ctx.request_headers(), m_options.header);
    if (incoming && valid_request_id(*incoming, m_options.max_incoming_length)) value.assign(*incoming);
  }

  if (value.empty()) {
    thread_local boost::uuids::random_generator generator;
    value = boost::uuids::to_string(generator());
  }

  auto& stored = ctx.storage().emplace<request_id_value>(request_id_value{std::move(value)});
  details::set_header(ctx.response_headers(), m_options.header, stored.value);
  next(std::move(handler));
}

} // namespace serveza::web::middleware
