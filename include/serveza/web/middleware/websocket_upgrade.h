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

#ifndef SERVEZA_WEB_MIDDLEWARE_WEBSOCKET_UPGRADE_H
#define SERVEZA_WEB_MIDDLEWARE_WEBSOCKET_UPGRADE_H

#include <type_traits>
#include <utility>

#include <serveza/web/context.h>
#include <serveza/web/websocket.h>

namespace serveza::web::middleware {

/**
 * @brief Terminal route handler that validates and accepts a WebSocket upgrade.
 *
 * The endpoint is stored directly, without @c std::function. Ordinary HTTP
 * requests receive @c 426 Upgrade Required. Endpoint state is shared by all
 * connections and must be safe for concurrent invocation.
 */
template<typename Endpoint>
class websocket_upgrade final {
public:
  /** @brief Stores @p endpoint and its connection @p options by value. */
  explicit websocket_upgrade(Endpoint endpoint, websocket_options options = {})
    : m_endpoint{std::move(endpoint)}
    , m_options{options}
  {
    static_assert(std::is_invocable_r_v<void, Endpoint&, websocket_connection&, completion_handler>,
                  "WebSocket endpoint must accept (websocket_connection&, completion_handler); wrap stackful and "
                  "C++20 endpoints with yield_websocket_endpoint or awaitable_websocket_endpoint");
    web::details::validate_websocket_options(m_options);
  }

  /** @brief Rejects an ordinary request or transfers the connection to the endpoint. */
  void operator()(request_context& ctx, completion_handler handler)
  {
    if (!ctx.websocket_upgrade_requested()) {
      ctx.response_headers().set(boost::beast::http::field::upgrade, "websocket");
      ctx.async_send_status(boost::beast::http::status::upgrade_required, std::move(handler));
      return;
    }
    ctx.async_accept_websocket(m_endpoint, m_options, std::move(handler));
  }

private:
  Endpoint m_endpoint;
  websocket_options m_options;
};

} // namespace serveza::web::middleware

#endif // SERVEZA_WEB_MIDDLEWARE_WEBSOCKET_UPGRADE_H
