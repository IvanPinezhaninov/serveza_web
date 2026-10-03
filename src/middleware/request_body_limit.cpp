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

#include <serveza/web/middleware/request_body_limit.h>

#include <serveza/web/context.h>
#include <serveza/web/middleware.h>

namespace serveza::web::middleware {

request_body_limit::request_body_limit(std::size_t limit)
  : m_limit{limit}
{}

void request_body_limit::operator()(request_context& ctx, continuation& next, completion_handler handler) const
{
  details::apply_request_body_limit(ctx, next, std::move(handler), m_limit);
}

namespace details {

void apply_request_body_limit(request_context& ctx, continuation& next, completion_handler handler, std::size_t limit)
{
  ctx.set_body_limit(limit);
  next(std::move(handler));
}

} // namespace details

} // namespace serveza::web::middleware
