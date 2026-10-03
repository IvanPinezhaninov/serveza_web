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

#include <serveza/serveza.h>
#include <serveza/web.h>
#include <serveza/web/yield.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <gtest/gtest.h>

#include "ServerHarness.h"

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace web = serveza::web;

web::application makeLifecycleApplication(std::chrono::seconds requestTimeout)
{
  web::settings settings;
  settings.request_timeout = requestTimeout;
  settings.keep_alive_timeout = requestTimeout;
  web::router routes{settings};
  routes.use(web::middleware::recover{});
  routes.post("/body", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
                ctx.async_send(http::status::ok, std::string{ctx.async_read_body(yield)}, yield);
              }});
  routes.get("/health", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
               ctx.async_send(http::status::ok, "healthy", yield);
             }});
  routes.get("/bytes", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
               web::byte_buffer body{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}};
               ctx.async_send(http::status::ok, std::move(body), yield);
             }});
  routes.get("/chunks", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
               ctx.async_send_chunked(
                   http::status::ok, "application/octet-stream",
                   web::yield_chunk_producer{[](web::response_writer& writer, net::yield_context innerYield) {
                     writer.async_write("first", innerYield);
                     const web::byte_buffer bytes{std::byte{0x00}, std::byte{0xff}};
                     writer.async_write(web::byte_view{bytes}, innerYield);
                   }},
                   yield);
             }});
  return std::move(routes).build();
}

class RunningHttpServer final {
public:
  explicit RunningHttpServer(web::application application)
    : m_server{[application = std::move(application)] { return web::http_session{application}; }, options()}
  {}

  RunningHttpServer(const RunningHttpServer&) = delete;

  RunningHttpServer& operator=(const RunningHttpServer&) = delete;

  [[nodiscard]] net::ip::tcp::endpoint endpoint() const
  {
    return {net::ip::address_v4::loopback(), m_server.port()};
  }

  [[nodiscard]] std::size_t activeConnections() const noexcept
  {
    return m_server.server().active_sessions();
  }

  void stop()
  {
    m_server.stop();
  }

private:
  static serveza::listener_options options()
  {
    serveza::listener_options value;
    value.shutdown_grace_period = std::chrono::milliseconds{250};
    return value;
  }

  ServerHarness m_server;
};

void writePartialBody(net::ip::tcp::socket& socket, std::size_t declaredSize, std::string_view fragment)
{
  const std::string header =
      "POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Length: " + std::to_string(declaredSize) +
      "\r\nConnection: keep-alive\r\n\r\n";
  net::write(socket, net::buffer(header));
  net::write(socket, net::buffer(fragment.data(), fragment.size()));
}

void expectHealthy(const net::ip::tcp::endpoint& endpoint)
{
  net::io_context io;
  net::ip::tcp::socket socket{io};
  socket.connect(endpoint);
  http::request<http::empty_body> request{http::verb::get, "/health", 11};
  request.set(http::field::host, "localhost");
  request.keep_alive(false);
  http::write(socket, request);
  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(socket, buffer, response);
  ASSERT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response.body(), "healthy");
}

TEST(HttpLifecycleIntegrationTest, SlowBodyIsTimedOutWithoutPoisoningServer)
{
  RunningHttpServer server{makeLifecycleApplication(std::chrono::seconds{1})};
  net::io_context io;
  net::ip::tcp::socket socket{io};
  socket.connect(server.endpoint());
  writePartialBody(socket, 8, "x");

  const auto started = std::chrono::steady_clock::now();
  char byte{};
  boost::system::error_code ec;
  const auto received = socket.read_some(net::buffer(&byte, 1), ec);
  const auto elapsed = std::chrono::steady_clock::now() - started;

  EXPECT_EQ(received, 0U);
  EXPECT_TRUE(ec == net::error::eof || ec == net::error::connection_reset || ec == net::error::operation_aborted)
      << ec.message();
  EXPECT_GE(elapsed, std::chrono::milliseconds{500});
  EXPECT_LT(elapsed, std::chrono::seconds{3});
  expectHealthy(server.endpoint());
}

TEST(HttpLifecycleIntegrationTest, ClientDisconnectDuringBodyDoesNotPoisonServer)
{
  RunningHttpServer server{makeLifecycleApplication(std::chrono::seconds{5})};
  net::io_context io;
  for (int i = 0; i < 16; ++i) {
    net::ip::tcp::socket socket{io};
    socket.connect(server.endpoint());
    writePartialBody(socket, 8, "x");
    boost::system::error_code ignored;
    socket.close(ignored);
  }

  for (int i = 0; i < 100 && server.activeConnections() != 0; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  expectHealthy(server.endpoint());
}

TEST(HttpLifecycleIntegrationTest, MalformedTargetReturnsBadRequestAndServerContinues)
{
  RunningHttpServer server{makeLifecycleApplication(std::chrono::seconds{5})};
  net::io_context io;
  net::ip::tcp::socket socket{io};
  socket.connect(server.endpoint());
  constexpr std::string_view malformed = "GET /invalid%ZZ HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
  net::write(socket, net::buffer(malformed.data(), malformed.size()));

  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(socket, buffer, response);
  EXPECT_EQ(response.result(), http::status::bad_request);
  EXPECT_FALSE(response.keep_alive());
  expectHealthy(server.endpoint());
}

TEST(HttpLifecycleIntegrationTest, AccessLogObservesSessionGeneratedErrorResponse)
{
  std::atomic_uint status{};
  std::atomic_size_t responseBodySize{};
  std::atomic_bool failure{};
  web::router routes;
  routes.use(web::middleware::access_log{[&](const web::middleware::access_log_entry& entry) {
    status = entry.status ? static_cast<unsigned>(*entry.status) : 0U;
    responseBodySize = entry.response_body_size;
    failure = static_cast<bool>(entry.error);
  }});
  routes.get("/boom",
             web::yield_layer{[](web::request_context&, net::yield_context) { throw std::runtime_error{"failure"}; }});

  RunningHttpServer server{std::move(routes).build()};
  net::io_context io;
  net::ip::tcp::socket socket{io};
  socket.connect(server.endpoint());
  http::request<http::empty_body> request{http::verb::get, "/boom", 11};
  request.set(http::field::host, "localhost");
  request.keep_alive(false);
  http::write(socket, request);
  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(socket, buffer, response);

  ASSERT_EQ(response.result(), http::status::internal_server_error);
  for (int i = 0; i < 100 && status.load() == 0U; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  EXPECT_EQ(status.load(), static_cast<unsigned>(http::status::internal_server_error));
  EXPECT_EQ(responseBodySize.load(), response.body().size());
  EXPECT_TRUE(failure.load());
}

TEST(HttpLifecycleIntegrationTest, ServerShutdownCancelsAStalledBodyRead)
{
  RunningHttpServer server{makeLifecycleApplication(std::chrono::seconds{30})};
  net::io_context io;
  std::vector<net::ip::tcp::socket> sockets;
  sockets.reserve(8);
  for (int i = 0; i < 8; ++i) {
    sockets.emplace_back(io);
    sockets.back().connect(server.endpoint());
    writePartialBody(sockets.back(), 8, "x");
  }
  for (int i = 0; i < 100 && server.activeConnections() != sockets.size(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  ASSERT_EQ(server.activeConnections(), sockets.size());

  const auto started = std::chrono::steady_clock::now();
  server.stop();
  const auto elapsed = std::chrono::steady_clock::now() - started;

  EXPECT_LT(elapsed, std::chrono::seconds{2});
  EXPECT_EQ(server.activeConnections(), 0U);
}

TEST(HttpLifecycleIntegrationTest, SendsByteAndChunkedResponsesOnTheWire)
{
  RunningHttpServer server{makeLifecycleApplication(std::chrono::seconds{5})};
  net::io_context io;
  net::ip::tcp::socket socket{io};
  socket.connect(server.endpoint());
  beast::flat_buffer buffer;

  http::request<http::empty_body> byteRequest{http::verb::get, "/bytes", 11};
  byteRequest.set(http::field::host, "localhost");
  byteRequest.keep_alive(true);
  http::write(socket, byteRequest);
  http::response<http::string_body> byteResponse;
  http::read(socket, buffer, byteResponse);
  ASSERT_EQ(byteResponse.result(), http::status::ok);
  ASSERT_EQ(byteResponse.body().size(), 3U);
  EXPECT_EQ(static_cast<unsigned char>(byteResponse.body()[0]), 0U);
  EXPECT_EQ(static_cast<unsigned char>(byteResponse.body()[2]), 0xffU);
  EXPECT_EQ(byteResponse[http::field::content_type], "application/octet-stream");

  http::request<http::empty_body> chunkRequest{http::verb::get, "/chunks", 11};
  chunkRequest.set(http::field::host, "localhost");
  chunkRequest.keep_alive(false);
  http::write(socket, chunkRequest);
  http::response<http::string_body> chunkResponse;
  http::read(socket, buffer, chunkResponse);
  ASSERT_EQ(chunkResponse.result(), http::status::ok);
  EXPECT_TRUE(chunkResponse.chunked());
  ASSERT_EQ(chunkResponse.body().size(), 7U);
  EXPECT_EQ(chunkResponse.body().substr(0, 5), "first");
  EXPECT_EQ(static_cast<unsigned char>(chunkResponse.body()[5]), 0U);
  EXPECT_EQ(static_cast<unsigned char>(chunkResponse.body()[6]), 0xffU);
}

TEST(HttpLifecycleIntegrationTest, ServesConcurrentPipelinedKeepAliveBursts)
{
  RunningHttpServer server{makeLifecycleApplication(std::chrono::seconds{5})};
  constexpr int clientCount = 12;
  constexpr int batchesPerClient = 4;
  constexpr int requestsPerBatch = 8;

  std::mutex failureMutex;
  std::exception_ptr failure;
  std::vector<std::thread> clients;
  clients.reserve(clientCount);

  for (int client = 0; client < clientCount; ++client) {
    clients.emplace_back([&] {
      try {
        net::io_context io;
        net::ip::tcp::socket socket{io};
        socket.connect(server.endpoint());
        beast::flat_buffer buffer;

        for (int batch = 0; batch < batchesPerClient; ++batch) {
          for (int requestIndex = 0; requestIndex < requestsPerBatch; ++requestIndex) {
            http::request<http::empty_body> request{http::verb::get, "/health", 11};
            request.set(http::field::host, "localhost");
            const bool last = batch + 1 == batchesPerClient && requestIndex + 1 == requestsPerBatch;
            request.keep_alive(!last);
            http::write(socket, request);
          }

          for (int requestIndex = 0; requestIndex < requestsPerBatch; ++requestIndex) {
            http::response<http::string_body> response;
            http::read(socket, buffer, response);
            if (response.result() != http::status::ok || response.body() != "healthy")
              throw std::runtime_error{"concurrent pipelined request returned an unexpected response"};
          }
        }
      } catch (...) {
        std::lock_guard lock{failureMutex};
        if (!failure) failure = std::current_exception();
      }
    });
  }

  for (auto& client : clients)
    client.join();
  if (failure) std::rethrow_exception(failure);

  expectHealthy(server.endpoint());
}

} // namespace
