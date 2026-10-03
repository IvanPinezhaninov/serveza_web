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

#include <array>
#include <chrono>
#include <future>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <gtest/gtest.h>

#include "ServerHarness.h"

namespace {

namespace beast = boost::beast;
namespace net = boost::asio;
namespace websocket = beast::websocket;
namespace web = serveza::web;

std::string readText(websocket::stream<net::ip::tcp::socket>& socket)
{
  beast::flat_buffer buffer;
  socket.read(buffer);
  if (!socket.got_text()) throw std::runtime_error{"expected a WebSocket text message"};
  return beast::buffers_to_string(buffer.data());
}

} // namespace

TEST(WebSocketIntegrationTest, SupportsGreetingAndPushWhileWaitingForInput)
{
  std::promise<web::websocket_sender> senderReady;
  web::websocket_sender retainedSender;

  web::router routes;
  web::websocket_options options;
  options.max_pending_messages = 16;
  options.max_pending_bytes = 16;
  routes.get("/events",
             web::middleware::websocket_upgrade{
                 web::yield_websocket_endpoint{[&](web::websocket_connection& connection, net::yield_context yield) {
                   connection.async_write_text("welcome", yield);
                   senderReady.set_value(connection.sender());
                   while (const auto message = connection.async_read(yield)) {
                     if (message->is_text() && message->text() == "stop") return;
                   }
                 }},
                 options});

  const auto app = std::move(routes).build();
  ServerHarness server{[app] { return web::http_session{app}; }};
  net::io_context io;
  websocket::stream<net::ip::tcp::socket> socket{io};
  socket.next_layer().connect(net::ip::tcp::endpoint{net::ip::address_v4::loopback(), server.port()});
  socket.handshake("localhost", "/events");

  EXPECT_EQ(readText(socket), "welcome");
  retainedSender = senderReady.get_future().get();
  EXPECT_TRUE(retainedSender.is_open());
  EXPECT_EQ(retainedSender.try_send_text(std::string(17, 'x')), web::websocket_send_result::queue_full);

  EXPECT_EQ(retainedSender.try_send_text("first"), web::websocket_send_result::queued);
  EXPECT_EQ(retainedSender.try_send_text("second"), web::websocket_send_result::queued);
  EXPECT_EQ(readText(socket), "first");
  EXPECT_EQ(readText(socket), "second");

  web::byte_buffer binary{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}};
  EXPECT_EQ(retainedSender.try_send_binary(binary), web::websocket_send_result::queued);
  beast::flat_buffer binaryBuffer;
  socket.read(binaryBuffer);
  EXPECT_TRUE(socket.got_binary());
  std::array<std::byte, 3> received{};
  EXPECT_EQ(net::buffer_copy(net::buffer(received), binaryBuffer.data()), received.size());
  EXPECT_EQ(received[0], binary[0]);
  EXPECT_EQ(received[1], binary[1]);
  EXPECT_EQ(received[2], binary[2]);

  std::array<web::websocket_send_result, 2> concurrentResults{};
  std::thread firstSender{[&] { concurrentResults[0] = retainedSender.try_send_text("thread-a"); }};
  std::thread secondSender{[&] { concurrentResults[1] = retainedSender.try_send_text("thread-b"); }};
  firstSender.join();
  secondSender.join();
  EXPECT_EQ(concurrentResults[0], web::websocket_send_result::queued);
  EXPECT_EQ(concurrentResults[1], web::websocket_send_result::queued);
  const std::set<std::string> concurrentMessages{readText(socket), readText(socket)};
  EXPECT_EQ(concurrentMessages, (std::set<std::string>{"thread-a", "thread-b"}));

  socket.text(true);
  socket.write(net::buffer("stop", 4));
  beast::flat_buffer closeBuffer;
  boost::system::error_code closeEc;
  socket.read(closeBuffer, closeEc);
  EXPECT_EQ(closeEc, websocket::error::closed);

  for (int attempt = 0; attempt < 100 && retainedSender.is_open(); ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  EXPECT_FALSE(retainedSender.is_open());
  EXPECT_EQ(retainedSender.try_send_text("late"), web::websocket_send_result::closed);
}

TEST(WebSocketIntegrationTest, CloseTimeoutReleasesAStalledPeer)
{
  std::promise<web::websocket_sender> senderReady;
  web::router routes;
  web::websocket_options options;
  options.close_timeout = std::chrono::seconds{1};
  routes.get("/stalled",
             web::middleware::websocket_upgrade{
                 web::yield_websocket_endpoint{[&](web::websocket_connection& connection, net::yield_context) {
                   senderReady.set_value(connection.sender());
                 }},
                 options});

  const auto app = std::move(routes).build();
  ServerHarness server{[app] { return web::http_session{app}; }};
  net::io_context io;
  websocket::stream<net::ip::tcp::socket> socket{io};
  socket.next_layer().connect(net::ip::tcp::endpoint{net::ip::address_v4::loopback(), server.port()});
  socket.handshake("localhost", "/stalled");
  auto retainedSender = senderReady.get_future().get();

  for (int attempt = 0; attempt < 150 && server.listener()->active_sessions() != 0; ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  EXPECT_EQ(server.listener()->active_sessions(), 0U);
  EXPECT_FALSE(retainedSender.is_open());
  EXPECT_EQ(retainedSender.try_send_text("late"), web::websocket_send_result::closed);
}
