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

#include <serveza/web/middleware/recover.h>

#include <serveza/web/context.h>
#include <serveza/web/errors.h>
#include <serveza/web/middleware.h>

namespace serveza::web::middleware::details {

void invoke_recover(request_context& ctx, continuation& next, completion_handler handler,
                    const recover_options& options, void* observer, exception_callback callback)
{
  next([&ctx, options, observer, callback, handler = std::move(handler)](boost::system::error_code ec) mutable {
    if (!ec) {
      handler(boost::system::error_code{});
      return;
    }
    std::exception_ptr ep = ctx.take_exception();
    if (!ep) ep = std::make_exception_ptr(boost::system::system_error{ec});
    if (ctx.response_committed()) {
      handler(ec);
      return;
    }
    try {
      std::rethrow_exception(ep);
    } catch (const request_error& req) {
      ctx.close_after_response();
      ctx.async_send(req.status(), req.what(), std::move(handler));
      return;
    } catch (...) {
      try {
        callback(observer, ep);
      } catch (...) {}
    }
    if (options.close_connection) ctx.close_after_response();
    ctx.async_send(options.status, options.body, options.content_type, std::move(handler));
  });
}

} // namespace serveza::web::middleware::details
