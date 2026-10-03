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
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/beast/http.hpp>

#include <serveza/serveza.h>
#include <serveza/web.h>
#include <serveza/web/yield.h>

namespace {

namespace http = boost::beast::http;
namespace net = boost::asio;
namespace web = serveza::web;

class ConsoleTrafficLog final {
public:
  void operator()(const web::middleware::request_headers_event& event) const
  {
    write(event.context, "request ", http::to_string(event.context.method()), ' ', event.context.target().raw);
    printHeaders(event.context, "request", event.context.request_headers());
  }

  void operator()(const web::middleware::request_body_chunk_event& event) const
  {
    write(event.context, "request body chunk: ", event.chunk.size(), " bytes");
  }

  void operator()(const web::middleware::response_headers_event& event) const
  {
    write(event.context, "response ", static_cast<unsigned>(event.status));
    printHeaders(event.context, "response", event.headers);
  }

  void operator()(const web::middleware::response_body_chunk_event& event) const
  {
    write(event.context, "response body chunk: ", event.chunk.size(), " bytes");
  }

  void operator()(const web::middleware::response_file_event& event) const
  {
    write(event.context, "response file ", event.path, ": ", event.size, " bytes");
  }

  void operator()(const web::middleware::exchange_complete_event& event) const
  {
    write(event.context, "request ", event.error ? "failed" : "complete", " in ", event.elapsed.count(), " us");
  }

private:
  static bool sensitive(http::field name, boost::beast::string_view text)
  {
    return name == http::field::authorization || name == http::field::cookie || name == http::field::set_cookie ||
           boost::beast::iequals(text, "X-Api-Key");
  }

  static std::string_view requestId(const web::request_context& ctx) noexcept
  {
    const auto* value = ctx.storage().get<web::middleware::request_id_value>();
    return value ? std::string_view{value->value} : std::string_view{"-"};
  }

  static std::mutex& outputMutex()
  {
    static std::mutex value;
    return value;
  }

  static void printHeaders(const web::request_context& ctx, const char* direction, const http::fields& headers)
  {
    for (const auto& field : headers) {
      if (sensitive(field.name(), field.name_string()))
        write(ctx, direction, " header ", field.name_string(), ": <redacted>");
      else
        write(ctx, direction, " header ", field.name_string(), ": ", field.value());
    }
  }

  template<typename... Values>
  static void write(const web::request_context& ctx, Values&&... values)
  {
    std::lock_guard lock{outputMutex()};
    std::cout << '[' << requestId(ctx) << "] ";
    (std::cout << ... << std::forward<Values>(values));
    std::cout << std::endl;
  }
};

class EchoEndpoint final {
public:
  void operator()(web::request_context& ctx, net::yield_context yield) const
  {
    const auto contentType = ctx.request_headers()[http::field::content_type];
    ctx.async_send(http::status::ok, std::string{ctx.async_read_body(yield)},
                   std::string_view{contentType.data(), contentType.size()}, yield);
  }
};

} // namespace

int main()
{
  try {
    web::router routes;
    routes.use(web::middleware::request_id{});
    routes.use(web::middleware::observe_traffic{ConsoleTrafficLog{}});
    routes.post("/echo", web::yield_layer{EchoEndpoint{}});

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
      std::cout << "Listening on http://" << listener->endpoint() << '\n' << "  POST /echo" << std::endl;
    });

    io.run();
    return result;
  } catch (const std::exception& e) {
    std::cerr << "Example failed: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}
