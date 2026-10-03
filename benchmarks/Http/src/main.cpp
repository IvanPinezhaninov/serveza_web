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

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/system/system_error.hpp>

#include <serveza/serveza.h>
#include <serveza/web.h>
#include <serveza/web/yield.h>
#include <serveza/yield_session.h>

#if defined(SERVEZA_WEB_HTTP_CPP20)
#include <serveza/web/awaitable.h>
#endif

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace sys = boost::system;
namespace web = serveza::web;
using namespace std::chrono_literals;

class SocketDeadline final {
public:
  explicit SocketDeadline(net::ip::tcp::socket& socket)
    : m_socket{socket}
    , m_timer{m_socket.get_executor()}
  {}

  ~SocketDeadline()
  {
    cancel();
  }

  void arm(std::chrono::seconds timeout)
  {
    m_timer.expires_after(timeout);
    m_timer.async_wait([socket = &m_socket](const sys::error_code& ec) {
      if (ec || !socket->is_open()) return;
      sys::error_code ignored;
      socket->cancel(ignored);
      socket->close(ignored);
    });
  }

  void cancel() noexcept
  {
    try {
      m_timer.cancel();
    } catch (...) {}
  }

private:
  net::ip::tcp::socket& m_socket;
  net::steady_timer m_timer;
};

bool expectedDisconnect(const sys::error_code& ec) noexcept
{
  return ec == http::error::end_of_stream || ec == net::error::eof || ec == net::error::connection_reset ||
         ec == net::error::connection_aborted || ec == net::error::broken_pipe || ec == net::error::operation_aborted;
}

// Transport-only baseline matching the mechanics used by web::http_session.
class RawHttpSession final {
public:
  void operator()(serveza::session_context<net::ip::tcp>& ctx, net::yield_context yield) const
  {
    auto& socket = ctx.socket();
    SocketDeadline deadline{socket};
    beast::flat_buffer buffer{1024 * 1024 + 16 * 1024};
    bool firstRequest = true;

    while (yield.cancelled() == net::cancellation_type::none) {
      http::request_parser<http::buffer_body> parser;
      parser.header_limit(16 * 1024);
      parser.body_limit(1024 * 1024);

      try {
        deadline.arm(firstRequest ? 10s : 60s);
        http::async_read_header(socket, buffer, parser, yield);
        deadline.cancel();

        std::array<char, 8192> bodyBuffer{};
        while (!parser.is_done()) {
          parser.get().body().data = bodyBuffer.data();
          parser.get().body().size = bodyBuffer.size();
          deadline.arm(10s);
          http::async_read_some(socket, buffer, parser, yield);
          deadline.cancel();
        }

        http::response<http::string_body> response{http::status::ok, parser.get().version()};
        response.set(http::field::content_type, "text/plain; charset=utf-8");
        response.keep_alive(parser.get().keep_alive());
        response.body() = "ok";
        response.prepare_payload();

        deadline.arm(10s);
        http::response_serializer<http::string_body> serializer{response};
        http::async_write(socket, serializer, yield);
        deadline.cancel();
        if (!response.keep_alive()) return;
        firstRequest = false;
      } catch (const sys::system_error& e) {
        deadline.cancel();
        if (expectedDisconnect(e.code())) return;
        throw;
      }
    }
  }
};

void sendOkCallback(web::request_context& ctx, web::completion_handler handler)
{
  ctx.async_send(http::status::ok, "ok", std::move(handler));
}

void sendOkYield(web::request_context& ctx, net::yield_context yield)
{
  ctx.async_send(http::status::ok, "ok", yield);
}

#if defined(SERVEZA_WEB_HTTP_CPP20)
net::awaitable<void> sendOkAwaitable(web::request_context& ctx)
{
  co_await ctx.async_send(http::status::ok, "ok", net::use_awaitable);
}
#endif

struct RoutedBenchmark final {
  web::application m_application;
  std::string m_target;
};

RoutedBenchmark makeRoutedBenchmark(std::string_view mode)
{
  web::router routes;
  std::string target;

  if (mode == "callback") {
    routes.get("/exact", &sendOkCallback);
    target = "/exact";
#if defined(SERVEZA_WEB_HTTP_CPP20)
  } else if (mode == "awaitable") {
    routes.get("/exact", web::awaitable_layer{&sendOkAwaitable});
    target = "/exact";
#endif
  } else if (mode == "passthrough") {
    routes.use(web::yield_layer{&sendOkYield});
    target = "/exact";
  } else if (mode == "exact") {
    routes.get("/exact", web::yield_layer{&sendOkYield});
    target = "/exact";
  } else if (mode == "dynamic") {
    routes.get("/users/:id", web::yield_layer{&sendOkYield});
    target = "/users/42";
  } else if (mode == "decimal") {
    routes.get("/users/:id([0-9]+)", web::yield_layer{&sendOkYield});
    target = "/users/42";
  } else if (mode == "regex") {
    routes.get("/users/:id([0-9]{1,8})", web::yield_layer{&sendOkYield});
    target = "/users/42";
  } else if (mode == "indexed") {
    for (std::size_t i = 0; i < 1000; ++i)
      routes.get("/route" + std::to_string(i) + "/value", web::yield_layer{&sendOkYield});
    target = "/route999/value";
  } else if (mode == "bucket-first" || mode == "bucket-last") {
    for (std::size_t i = 0; i < 1000; ++i)
      routes.get("/api/item" + std::to_string(i), web::yield_layer{&sendOkYield});
    target = mode == "bucket-first" ? "/api/item0" : "/api/item999";
  } else if (mode == "fallback-first" || mode == "fallback-last") {
    for (std::size_t i = 0; i < 1000; ++i)
      routes.get("/:tenant/item" + std::to_string(i), web::yield_layer{&sendOkYield});
    target = mode == "fallback-first" ? "/tenant/item0" : "/tenant/item999";
  } else {
    throw std::invalid_argument{"unknown routed benchmark mode"};
  }

  return {std::move(routes).build(), std::move(target)};
}

unsigned short parsePort(const char* value)
{
  const auto number = std::stoul(value);
  if (number == 0 || number > std::numeric_limits<unsigned short>::max())
    throw std::invalid_argument{"port must be between 1 and 65535"};
  return static_cast<unsigned short>(number);
}

unsigned int parseThreads(const char* value)
{
  const auto number = std::stoul(value);
  if (number == 0 || number > std::numeric_limits<unsigned int>::max())
    throw std::invalid_argument{"threads must be positive"};
  return static_cast<unsigned int>(number);
}

void printUsage(const char* program)
{
  std::cerr << "usage: " << program
            << " <raw|callback|"
#if defined(SERVEZA_WEB_HTTP_CPP20)
               "awaitable|"
#endif
               "passthrough|exact|dynamic|decimal|regex|indexed|bucket-first|bucket-last|fallback-first|"
               "fallback-last>"
               " [port=18080] [threads=1]\n";
}

} // namespace

int main(int argc, char* argv[])
{
  if (argc < 2 || argc > 4) {
    printUsage(argv[0]);
    return EXIT_FAILURE;
  }

  try {
    const std::string mode = argv[1];
    const auto port = argc >= 3 ? parsePort(argv[2]) : static_cast<unsigned short>(18080);
    const auto threads = argc >= 4 ? parseThreads(argv[3]) : 1U;

    net::io_context io{static_cast<int>(threads)};
    serveza::server server{io.get_executor()};
    const net::ip::tcp::endpoint endpoint{net::ip::address_v4::loopback(), port};
    std::string target = "/exact";
    std::shared_ptr<serveza::listener> listener;
    if (mode == "raw") {
      listener = server.listen<net::ip::tcp>(endpoint, [] { return serveza::yield_session{RawHttpSession{}}; });
    } else {
      auto benchmark = makeRoutedBenchmark(mode);
      target = std::move(benchmark.m_target);
      listener = server.listen<net::ip::tcp>(
          endpoint, [application = std::move(benchmark.m_application)] { return web::http_session{application}; });
    }
    (void)listener;

    net::signal_set signals{io, SIGINT, SIGTERM};
    signals.async_wait([&server](const sys::error_code& ec, int) {
      if (!ec) server.request_stop();
    });

    std::atomic_bool failed{};
    server.async_wait([&](sys::error_code ec) {
      if (ec) failed = true;
      signals.cancel();
    });
    server.async_start([&](sys::error_code ec) {
      if (!ec) return;
      failed = true;
      server.request_stop();
    });

    std::cout << "mode=" << mode << " url=http://127.0.0.1:" << port << target << " threads=" << threads << '\n'
              << std::flush;
    std::vector<std::thread> workers;
    workers.reserve(threads - 1U);
    for (unsigned int i = 1; i < threads; ++i)
      workers.emplace_back([&io] { io.run(); });
    io.run();
    for (auto& worker : workers)
      worker.join();
    return failed.load() ? EXIT_FAILURE : EXIT_SUCCESS;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
