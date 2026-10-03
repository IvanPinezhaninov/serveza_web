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

#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

#include <serveza/serveza.h>
#include <serveza/web.h>
#include <serveza/web/yield.h>

namespace {

namespace http = boost::beast::http;
namespace net = boost::asio;
namespace web = serveza::web;

class UserEndpoint final {
public:
  void operator()(web::request_context& ctx, net::yield_context yield) const
  {
    ctx.async_send(http::status::ok, "user " + std::string{ctx.params().at("id")}, yield);
  }
};

class SearchEndpoint final {
public:
  void operator()(web::request_context& ctx, net::yield_context yield) const
  {
    const auto query = ctx.target().query.first("q");
    ctx.async_send(http::status::ok, query ? std::string{*query} : "no query", yield);
  }
};

} // namespace

int main()
{
  try {
    web::router api;
    api.get("/users/:id([0-9]+)", web::yield_layer{UserEndpoint{}});
    api.get("/search", web::yield_layer{SearchEndpoint{}});

    web::router routes;
    routes.mount("/api", std::move(api));

    const auto app = std::move(routes).build();
    net::io_context io;
    serveza::server server{io.get_executor()};
    auto listener =
        server.listen<net::ip::tcp>({net::ip::address_v4::loopback(), 8080}, [app] { return web::http_session{app}; });

    net::signal_set signals{io, SIGINT, SIGTERM};
    signals.async_wait([&server](boost::system::error_code ec, int) {
      if (!ec) server.request_stop();
    });

    int result = EXIT_SUCCESS;
    server.async_wait([&](boost::system::error_code ec) {
      if (ec) {
        std::cerr << "Server stopped with error: " << ec.message() << std::endl;
        result = EXIT_FAILURE;
      }
      signals.cancel();
    });
    server.async_start([&, listener](boost::system::error_code ec) {
      if (ec) {
        std::cerr << "Server start failed: " << ec.message() << std::endl;
        result = EXIT_FAILURE;
        server.request_stop();
        return;
      }
      std::cout << "Listening on http://" << listener->endpoint() << '\n'
                << "  GET /api/users/42\n"
                << "  GET /api/search?q=serveza" << std::endl;
    });

    io.run();
    return result;
  } catch (const std::exception& e) {
    std::cerr << "Example failed: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}
