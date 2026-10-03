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

#include <serveza/serveza.h>
#include <serveza/web.h>
#include <serveza/web/yield.h>

namespace {

class EchoEndpoint final {
public:
  void operator()(serveza::web::websocket_connection& ws, boost::asio::yield_context yield) const
  {
    ws.async_write_text("Hello! Send me a text or binary message, and I will echo it back.", yield);

    while (const auto msg = ws.async_read(yield)) {
      if (msg->is_text())
        ws.async_write_text(std::string{msg->text()}, yield);
      else
        ws.async_write_binary(msg->bytes(), yield);
    }
  }
};

} // namespace

int main()
{
  namespace net = boost::asio;
  namespace web = serveza::web;

  try {
    web::router routes;
    routes.get("/echo", web::middleware::websocket_upgrade{web::yield_websocket_endpoint{EchoEndpoint{}}});

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
      std::cout << "Listening on ws://" << listener->endpoint() << "/echo" << std::endl;
    });

    io.run();
    return result;
  } catch (const std::exception& e) {
    std::cerr << "Example failed: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}
