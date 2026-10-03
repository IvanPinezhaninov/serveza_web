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

#ifndef SERVEZA_WEB_SRC_DETAIL_HTTP_DATE_H
#define SERVEZA_WEB_SRC_DETAIL_HTTP_DATE_H

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace serveza::web::details {

std::string format_http_date(std::chrono::system_clock::time_point value);

std::optional<std::chrono::system_clock::time_point> parse_http_date(std::string_view value);

} // namespace serveza::web::details

#endif // SERVEZA_WEB_SRC_DETAIL_HTTP_DATE_H
