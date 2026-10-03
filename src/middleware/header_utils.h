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

#ifndef SERVEZA_WEB_SRC_MIDDLEWARE_HEADER_UTILS_H
#define SERVEZA_WEB_SRC_MIDDLEWARE_HEADER_UTILS_H

#include <optional>
#include <string_view>

#include <boost/beast/http/fields.hpp>

namespace serveza::web::middleware::details {

inline std::optional<std::string_view> header_value(const boost::beast::http::fields& fields, std::string_view name)
{
  const auto it = fields.find(boost::beast::string_view{name.data(), name.size()});
  if (it == fields.end()) return std::nullopt;
  return std::string_view{it->value().data(), it->value().size()};
}

inline void set_header(boost::beast::http::fields& fields, std::string_view name, std::string_view value)
{
  fields.set(boost::beast::string_view{name.data(), name.size()},
             boost::beast::string_view{value.data(), value.size()});
}

} // namespace serveza::web::middleware::details

#endif // SERVEZA_WEB_SRC_MIDDLEWARE_HEADER_UTILS_H
