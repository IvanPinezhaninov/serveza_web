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

#include <serveza/web/middleware/security_headers.h>

#include <utility>

#include <serveza/web/context.h>
#include <serveza/web/middleware.h>

#include "header_utils.h"

namespace serveza::web::middleware {

security_headers::security_headers(security_headers_options options)
  : m_options{std::move(options)}
{}

void security_headers::operator()(request_context& ctx, continuation& next, completion_handler handler) const
{
  auto& headers = ctx.response_headers();
  if (m_options.prevent_content_type_sniffing) details::set_header(headers, "X-Content-Type-Options", "nosniff");
  if (m_options.deny_framing) details::set_header(headers, "X-Frame-Options", "DENY");
  if (!m_options.referrer_policy.empty()) details::set_header(headers, "Referrer-Policy", m_options.referrer_policy);
  if (!m_options.content_security_policy.empty())
    details::set_header(headers, "Content-Security-Policy", m_options.content_security_policy);
  if (!m_options.permissions_policy.empty())
    details::set_header(headers, "Permissions-Policy", m_options.permissions_policy);
  if (!m_options.strict_transport_security.empty())
    details::set_header(headers, "Strict-Transport-Security", m_options.strict_transport_security);
  next(std::move(handler));
}

} // namespace serveza::web::middleware
