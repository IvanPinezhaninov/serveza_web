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

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <gtest/gtest.h>

#include "ServerHarness.h"
#include "TestCertificate.h"

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
namespace web = serveza::web;

web::application makeTlsApplication()
{
  web::router routes;
  routes.get("/secure/:value", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
               ctx.async_send(http::status::ok, "secure " + std::string{ctx.params().at("value")}, yield);
             }});
  routes.get("/ws", web::middleware::websocket_upgrade{web::yield_websocket_endpoint{
                        [](web::websocket_connection& connection, net::yield_context yield) {
                          connection.async_write_text("secure welcome", yield);
                          const auto message = connection.async_read(yield);
                          if (message) {
                            if (message->is_text())
                              connection.async_write_text(std::string{message->text()}, yield);
                            else
                              connection.async_write_binary(message->bytes(), yield);
                          }
                        }}});
  return std::move(routes).build();
}

class RunningHttpsServer final {
public:
  explicit RunningHttpsServer(web::application application)
  {
    loadTestCertificate(m_tls);
    serveza::listener_options options;
    options.shutdown_grace_period = std::chrono::milliseconds{250};
    m_server = std::make_unique<ServerHarness>(
        [application = std::move(application), this] { return web::https_session{application, m_tls}; }, options);
  }

  ~RunningHttpsServer()
  {
    stopNoexcept();
  }

  RunningHttpsServer(const RunningHttpsServer&) = delete;

  RunningHttpsServer& operator=(const RunningHttpsServer&) = delete;

  [[nodiscard]] net::ip::tcp::endpoint endpoint() const
  {
    return {net::ip::address_v4::loopback(), m_server->port()};
  }

  [[nodiscard]] std::size_t activeConnections() const noexcept
  {
    return m_server->server().active_sessions();
  }

  void stop()
  {
    if (m_server) m_server->stop();
  }

private:
  void stopNoexcept() noexcept
  {
    try {
      if (m_server) m_server->stop();
    } catch (...) {}
  }

  ssl::context m_tls{ssl::context::tls_server};
  std::unique_ptr<ServerHarness> m_server;
};

void expectSecureHealth(const net::ip::tcp::endpoint& endpoint)
{
  net::io_context io;
  ssl::context clientTls{ssl::context::tls_client};
  clientTls.set_verify_mode(ssl::verify_none);
  ssl::stream<net::ip::tcp::socket> stream{io, clientTls};
  stream.next_layer().connect(endpoint);
  stream.handshake(ssl::stream_base::client);

  http::request<http::empty_body> request{http::verb::get, "/secure/health", 11};
  request.set(http::field::host, "localhost");
  request.keep_alive(false);
  http::write(stream, request);

  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(stream, buffer, response);
  ASSERT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response.body(), "secure health");

  boost::system::error_code ignored;
  stream.shutdown(ignored);
}

} // namespace

TEST(HttpsIntegrationTest, SharesApplicationAcrossTlsAndWebSocketTransports)
{
  RunningHttpsServer server{makeTlsApplication()};
  net::io_context io;
  ssl::context clientTls{ssl::context::tls_client};
  clientTls.set_verify_mode(ssl::verify_none);
  ssl::stream<net::ip::tcp::socket> stream{io, clientTls};
  stream.next_layer().connect(server.endpoint());
  stream.handshake(ssl::stream_base::client);

  http::request<http::empty_body> request{http::verb::get, "/secure/channel", 11};
  request.set(http::field::host, "localhost");
  request.keep_alive(false);
  http::write(stream, request);

  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(stream, buffer, response);
  if (response.result() != http::status::ok || response.body() != "secure channel")
    throw std::runtime_error{"HTTPS request did not reach the shared application"};

  boost::system::error_code ignored;
  stream.shutdown(ignored);

  ssl::stream<net::ip::tcp::socket> rejected{io, clientTls};
  rejected.next_layer().connect(server.endpoint());
  rejected.handshake(ssl::stream_base::client);
  http::request<http::empty_body> ordinary{http::verb::get, "/ws", 11};
  ordinary.set(http::field::host, "localhost");
  ordinary.keep_alive(false);
  http::write(rejected, ordinary);
  beast::flat_buffer rejectedBuffer;
  http::response<http::string_body> rejectedResponse;
  http::read(rejected, rejectedBuffer, rejectedResponse);
  if (rejectedResponse.result() != http::status::upgrade_required)
    throw std::runtime_error{"ordinary HTTPS request to WebSocket endpoint was not rejected"};
  rejected.shutdown(ignored);

  websocket::stream<ssl::stream<net::ip::tcp::socket>> ws{io, clientTls};
  ws.next_layer().next_layer().connect(server.endpoint());
  ws.next_layer().handshake(ssl::stream_base::client);
  ws.handshake("localhost", "/ws");
  beast::flat_buffer greetingBuffer;
  ws.read(greetingBuffer);
  if (!ws.got_text() || beast::buffers_to_string(greetingBuffer.data()) != "secure welcome")
    throw std::runtime_error{"TLS WebSocket endpoint did not initiate a greeting"};
  constexpr std::string_view payload{"secure websocket"};
  ws.text(true);
  ws.write(net::buffer(payload.data(), payload.size()));
  beast::flat_buffer websocketBuffer;
  ws.read(websocketBuffer);
  if (!ws.got_text() || beast::buffers_to_string(websocketBuffer.data()) != payload)
    throw std::runtime_error{"TLS WebSocket endpoint did not echo a text message"};
  ws.close(websocket::close_code::normal, ignored);

  server.stop();
}

TEST(HttpsIntegrationTest, SurvivesFailedHandshakesAndAbruptDisconnects)
{
  RunningHttpsServer server{makeTlsApplication()};
  net::io_context io;

  for (int attempt = 0; attempt < 16; ++attempt) {
    net::ip::tcp::socket plaintext{io};
    plaintext.connect(server.endpoint());
    constexpr std::string_view invalidHandshake{"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"};
    net::write(plaintext, net::buffer(invalidHandshake.data(), invalidHandshake.size()));
    boost::system::error_code ignored;
    plaintext.close(ignored);

    ssl::context clientTls{ssl::context::tls_client};
    clientTls.set_verify_mode(ssl::verify_none);
    ssl::stream<net::ip::tcp::socket> stream{io, clientTls};
    stream.next_layer().connect(server.endpoint());
    stream.handshake(ssl::stream_base::client);
    stream.next_layer().close(ignored);
  }

  expectSecureHealth(server.endpoint());
  server.stop();
}

TEST(HttpsIntegrationTest, ShutdownCancelsStalledHandshakes)
{
  RunningHttpsServer server{makeTlsApplication()};
  net::io_context io;
  std::vector<net::ip::tcp::socket> sockets;
  sockets.reserve(8);
  for (int index = 0; index < 8; ++index) {
    sockets.emplace_back(io);
    sockets.back().connect(server.endpoint());
  }

  for (int attempt = 0; attempt < 100 && server.activeConnections() != sockets.size(); ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  ASSERT_EQ(server.activeConnections(), sockets.size());

  const auto started = std::chrono::steady_clock::now();
  server.stop();
  const auto elapsed = std::chrono::steady_clock::now() - started;

  EXPECT_LT(elapsed, std::chrono::seconds{2});
  EXPECT_EQ(server.activeConnections(), 0U);
}
