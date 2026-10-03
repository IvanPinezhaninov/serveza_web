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
#include <cstddef>
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

class ConsoleAccessLog final {
public:
  void operator()(const web::middleware::access_log_entry& entry) const
  {
    std::cout << http::to_string(entry.method) << ' ' << entry.path << ' '
              << (entry.status ? std::to_string(static_cast<unsigned>(*entry.status)) : "-") << std::endl;
  }
};

class UploadLimit final {
public:
  std::size_t operator()(const web::request_context& ctx) const noexcept
  {
    return ctx.method() == http::verb::post ? 64U * 1024U : 1024U;
  }
};

class ExceptionReporter final {
public:
  void operator()(std::exception_ptr ep) const noexcept
  {
    try {
      if (ep) std::rethrow_exception(ep);
    } catch (const std::exception& e) {
      std::cerr << "request failed: " << e.what() << std::endl;
    }
  }
};

class ExampleHeaderMiddleware final {
public:
  void operator()(web::request_context& ctx, web::continuation& next, net::yield_context yield) const
  {
    ctx.response_headers().set("X-Example-Middleware", "active");
    next(yield);
  }
};

class UploadEndpoint final {
public:
  void operator()(web::request_context& ctx, net::yield_context yield) const
  {
    const auto& body = ctx.async_read_body(yield);
    ctx.async_send(http::status::ok, "received " + std::to_string(body.size()) + " bytes", yield);
  }
};

} // namespace

int main()
{
  try {
    web::router routes;

    web::middleware::cors_options corsOptions;
    corsOptions.allow_any_origin = true;

    routes.use(web::middleware::recover{web::middleware::recover_options{}, ExceptionReporter{}});
    routes.use(web::middleware::access_log{ConsoleAccessLog{}});
    routes.use(web::middleware::request_id{});
    routes.use(web::middleware::security_headers{});
    routes.use(web::middleware::cors{std::move(corsOptions)});
    routes.use("/upload", web::middleware::request_body_limit_by{UploadLimit{}});

    routes.post("/upload", web::yield_layer{ExampleHeaderMiddleware{}});
    routes.post("/upload", web::yield_layer{UploadEndpoint{}});

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
      std::cout << "Listening on http://" << listener->endpoint() << '\n' << "  POST /upload" << std::endl;
    });

    io.run();
    return result;
  } catch (const std::exception& e) {
    std::cerr << "Example failed: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}
