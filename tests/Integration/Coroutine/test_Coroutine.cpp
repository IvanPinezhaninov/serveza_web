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

#include "ServerHarness.h"

#include <string>
#include <utility>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <gtest/gtest.h>

#include <serveza/web.h>
#include <serveza/web/awaitable.h>

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace web = serveza::web;

std::string request(ServerHarness& server, std::string target)
{
  net::io_context io;
  net::ip::tcp::socket socket{io};
  socket.connect(net::ip::tcp::endpoint{net::ip::address_v4::loopback(), server.port()});
  http::request<http::empty_body> req{http::verb::get, std::move(target), 11};
  req.set(http::field::host, "localhost");
  req.keep_alive(false);
  http::write(socket, req);
  beast::flat_buffer buffer;
  http::response<http::string_body> res;
  http::read(socket, buffer, res);
  EXPECT_EQ(res.result(), http::status::ok);
  return res.body();
}

} // namespace

TEST(CoroutineIntegrationTest, RunsCpp20CoroutineOnTheCxx17Core)
{
  web::router routes;
  routes.use("/coroutine",
             web::awaitable_layer{[](web::request_context&, web::continuation& next) -> net::awaitable<void> {
               co_await next(net::use_awaitable);
             }});
  routes.get("/coroutine", web::awaitable_layer{[](web::request_context& ctx) -> net::awaitable<void> {
               co_await ctx.async_send(http::status::ok, "coroutine", net::use_awaitable);
             }});

  const auto application = std::move(routes).build();
  ServerHarness server{[application] { return web::http_session{application}; }};

  EXPECT_EQ(request(server, "/coroutine"), "coroutine");
}
