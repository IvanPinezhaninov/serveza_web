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

#ifndef SERVEZA_WEB_SESSION_H
#define SERVEZA_WEB_SESSION_H

#include <utility>

#include <boost/asio/async_result.hpp>
#include <boost/asio/ip/tcp.hpp>

#if SERVEZA_WEB_USE_SSL
#include <boost/asio/ssl/context.hpp>
#endif

#include <serveza/session_context.h>
#include <serveza/web/application.h>
#include <serveza/web/async.h>
#include <serveza/web/export.h>
#include <serveza/web/settings.h>

namespace serveza::web {

/** @brief Serveza TCP session that runs an immutable HTTP application. */
class SERVEZA_WEB_API http_session final {
public:
  /** @brief Transport protocol accepted by this session. */
  using protocol_type = boost::asio::ip::tcp;

  /** @brief Creates an HTTP session factory sharing @p app with every accepted connection. */
  explicit http_session(application app);

  /** @brief Runs the HTTP/1 request loop. Completion signature: `void(error_code)`. */
  template<typename CompletionToken>
  auto async_run(serveza::session_context<protocol_type>& ctx, CompletionToken&& token)
  {
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, &ctx](auto handler) mutable { do_async_run(ctx, completion_handler{std::move(handler)}); }, token);
  }

private:
  void do_async_run(serveza::session_context<protocol_type>& ctx, completion_handler handler);

  application m_application;
  settings m_settings;
};

#if SERVEZA_WEB_USE_SSL
/** @brief Serveza TCP session that runs an immutable HTTP application over TLS. */
class SERVEZA_WEB_API https_session final {
public:
  /** @brief Transport protocol accepted by this session. */
  using protocol_type = boost::asio::ip::tcp;

  /** @brief Creates an HTTPS session factory using @p ssl_ctx for accepted connections. */
  https_session(application app, boost::asio::ssl::context& ssl_ctx);

  /** @brief Runs TLS and HTTP/1. Completion signature: `void(error_code)`. */
  template<typename CompletionToken>
  auto async_run(serveza::session_context<protocol_type>& ctx, CompletionToken&& token)
  {
    return boost::asio::async_initiate<CompletionToken, void(boost::system::error_code)>(
        [this, &ctx](auto handler) mutable { do_async_run(ctx, completion_handler{std::move(handler)}); }, token);
  }

private:
  void do_async_run(serveza::session_context<protocol_type>& ctx, completion_handler handler);

  application m_application;
  settings m_settings;
  boost::asio::ssl::context& m_ssl_ctx;
};
#endif

} // namespace serveza::web

#endif // SERVEZA_WEB_SESSION_H
