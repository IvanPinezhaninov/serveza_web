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

#ifndef SERVEZA_WEB_ERRORS_H
#define SERVEZA_WEB_ERRORS_H

#include <stdexcept>
#include <string>

#include <serveza/web/export.h>

namespace boost::beast::http {
enum class status : unsigned;
} // namespace boost::beast::http

namespace serveza::web {

/** @brief HTTP-aware request failure that can be converted into an error response. */
class SERVEZA_WEB_API request_error : public std::runtime_error {
public:
  /** @brief Creates an error with the response @p status and diagnostic @p message. */
  request_error(boost::beast::http::status status, std::string message);

  /** @brief Returns the HTTP status associated with this failure. */
  [[nodiscard]] boost::beast::http::status status() const noexcept;

private:
  boost::beast::http::status m_status;
};

} // namespace serveza::web

#endif // SERVEZA_WEB_ERRORS_H
