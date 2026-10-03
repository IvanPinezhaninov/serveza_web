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
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <gtest/gtest.h>

#include "ServerHarness.h"

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace web = serveza::web;

void expect(bool value, const char* message)
{
  if (!value) throw std::runtime_error{message};
}

template<typename Body>
http::response<http::string_body> exchange(net::ip::tcp::socket& socket, beast::flat_buffer& buffer,
                                           http::request<Body>& request)
{
  http::write(socket, request);
  http::response<http::string_body> response;
  http::read(socket, buffer, response);
  return response;
}

} // namespace

TEST(HttpIntegrationTest, ExercisesApplicationPipeline)
{
  std::atomic_int logCalls{};
  std::atomic_int logged405{};
  std::atomic_bool loggedRouteAndConnection{};
  web::settings settings;
  settings.body_limit = 32;
  settings.header_limit = 1024;
  web::router routes{settings};
  routes.use(web::middleware::access_log{[&](const web::middleware::access_log_entry& entry) {
    ++logCalls;
    if (entry.status == http::status::method_not_allowed) ++logged405;
    if (entry.route == "/users/:id" && entry.listener_id != 0 && entry.connection_id != 0 &&
        !entry.remote_endpoint.empty())
      loggedRouteAndConnection = true;
  }});
  routes.use(web::middleware::recover{});
  routes.use(web::middleware::request_id{});
  routes.use(web::middleware::security_headers{});
  web::middleware::cors_options corsOptions;
  corsOptions.allow_any_origin = true;
  corsOptions.allowed_headers = "Content-Type";
  routes.use(web::middleware::cors{std::move(corsOptions)});

  routes.get("/users/:id", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
               web::response_cookie cookie;
               cookie.name = "visited";
               cookie.value = "yes";
               cookie.path = "/";
               cookie.http_only = true;
               web::set_cookie(ctx, cookie);
               ctx.async_send(http::status::ok, "user " + std::string{ctx.params().at("id")}, yield);
             }});

  routes.post("/echo",
              web::yield_layer{[](web::request_context& ctx, web::continuation& next, net::yield_context yield) {
                ctx.storage().emplace<std::string>(ctx.async_read_body(yield));
                next(yield);
              }});
  routes.post("/echo", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
                ctx.async_send(http::status::ok, ctx.storage().require<std::string>(), yield);
              }});
  routes.post("/stream",
              web::yield_layer{[](web::request_context& ctx, web::continuation& next, net::yield_context yield) {
                auto& body = ctx.storage().emplace<std::string>();
                ctx.async_read_body_chunks([&](std::string_view chunk) { body.append(chunk); }, yield);
                next(yield);
              }});
  routes.post("/stream", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
                ctx.async_send(http::status::ok, ctx.storage().require<std::string>(), yield);
              }});
  routes.use("/limited", web::middleware::request_body_limit{4U});
  routes.post("/limited", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
                ctx.async_send(http::status::ok, std::string{ctx.async_read_body(yield)}, yield);
              }});
  routes.get("/one", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
               ctx.async_send(http::status::ok, "one", yield);
             }});
  routes.get("/two", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
               ctx.async_send(http::status::ok, "two", yield);
             }});

  std::atomic_bool nestedParamsRestored{};
  web::router tenant;
  tenant.use(web::yield_layer{[&](web::request_context& ctx, web::continuation& next, net::yield_context yield) {
    expect(ctx.param("tenant") == "acme", "dynamic mount parameter is missing in child middleware");
    expect(!ctx.param("id"), "child route parameter leaked before next");
    next(yield);
    expect(ctx.param("tenant") == "acme", "mount parameter was not restored after next");
    expect(!ctx.param("id"), "child route parameter leaked after next");
    nestedParamsRestored = true;
  }});
  tenant.get("/users/:id", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
               ctx.async_send(http::status::ok,
                              std::string{ctx.params().at("tenant")} + ':' + std::string{ctx.params().at("id")}, yield);
             }});
  routes.mount("/accounts/:tenant", std::move(tenant));

  web::router api;
  api.get("/health", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
            ctx.async_send(http::status::ok, "healthy", yield);
          }});
  routes.use("/api/", std::move(api));

  web::router rooms;
  rooms.get("/:roomId/state/:eventType{/:stateKey}",
            web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
              std::string body{ctx.params().at("roomId")};
              body += ':';
              body += ctx.params().at("eventType");
              if (const auto stateKey = ctx.param("stateKey")) {
                body += ':';
                body += *stateKey;
              }
              ctx.async_send(http::status::ok, std::move(body), yield);
            }});
  web::router client_v3;
  client_v3.use("/rooms", std::move(rooms));
  web::router matrix;
  matrix.use("/client/v3", std::move(client_v3));
  routes.use("/_matrix", std::move(matrix));

  routes.get("/boom", web::yield_layer{[](web::request_context&, net::yield_context) {
               throw std::runtime_error{"application failure"};
             }});
  routes.get("/ws", web::middleware::websocket_upgrade{web::yield_websocket_endpoint{
                        [](web::websocket_connection& connection, net::yield_context yield) {
                          while (const auto message = connection.async_read(yield)) {
                            if (message->is_text())
                              connection.async_write_text(std::string{message->text()}, yield);
                            else
                              connection.async_write_binary(message->bytes(), yield);
                          }
                        }}});

  auto app = std::move(routes).build();
  ServerHarness server{[app] { return web::http_session{app}; }};
  const auto port = server.port();
  net::io_context io;
  net::ip::tcp::socket socket{io};
  socket.connect(net::ip::tcp::endpoint{net::ip::address_v4::loopback(), port});
  beast::flat_buffer buffer;

  http::request<http::empty_body> get{http::verb::get, "/users/42", 11};
  get.set(http::field::host, "localhost");
  get.keep_alive(true);
  auto getResponse = exchange(socket, buffer, get);
  expect(getResponse.result() == http::status::ok, "GET route failed");
  expect(getResponse.body() == "user 42", "route parameter was not delivered");
  expect(getResponse.count("X-Request-ID") == 1, "request ID middleware did not run");
  expect(getResponse.count(http::field::set_cookie) == 1, "cookie response header is missing");
  expect(getResponse["X-Content-Type-Options"] == "nosniff", "security headers middleware did not run");

  http::request<http::string_body> post{http::verb::post, "/echo", 11};
  post.set(http::field::host, "localhost");
  post.keep_alive(true);
  post.body() = "payload";
  post.prepare_payload();
  auto postResponse = exchange(socket, buffer, post);
  expect(postResponse.result() == http::status::ok && postResponse.body() == "payload",
         "body middleware could not read and continue");

  http::request<http::string_body> streamed{http::verb::post, "/stream", 11};
  streamed.set(http::field::host, "localhost");
  streamed.keep_alive(true);
  streamed.body() = "streamed-payload";
  streamed.prepare_payload();
  const auto streamedResponse = exchange(socket, buffer, streamed);
  expect(streamedResponse.result() == http::status::ok && streamedResponse.body() == streamed.body(),
         "streaming body callback could not consume and continue");

  http::request<http::empty_body> nested{http::verb::get, "/accounts/acme/users/7", 11};
  nested.set(http::field::host, "localhost");
  nested.keep_alive(true);
  const auto nestedResponse = exchange(socket, buffer, nested);
  expect(nestedResponse.result() == http::status::ok && nestedResponse.body() == "acme:7",
         "dynamic nested router did not compose mount and route parameters");
  for (int i = 0; i < 50 && !nestedParamsRestored.load(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  expect(nestedParamsRestored.load(), "nested middleware parameter scope was not restored");

  http::request<http::empty_body> nestedStatic{http::verb::get, "/api/health", 11};
  nestedStatic.set(http::field::host, "localhost");
  nestedStatic.keep_alive(true);
  const auto nestedStaticResponse = exchange(socket, buffer, nestedStatic);
  expect(nestedStaticResponse.result() == http::status::ok && nestedStaticResponse.body() == "healthy",
         "static nested router did not match its composed path");

  http::request<http::empty_body> mountBoundary{http::verb::get, "/apiary/health", 11};
  mountBoundary.set(http::field::host, "localhost");
  mountBoundary.keep_alive(true);
  const auto mountBoundaryResponse = exchange(socket, buffer, mountBoundary);
  expect(mountBoundaryResponse.result() == http::status::not_found, "mount prefix crossed a segment boundary");

  http::request<http::empty_body> stateWithoutKey{http::verb::get, "/_matrix/client/v3/rooms/room/state/topic", 11};
  stateWithoutKey.set(http::field::host, "localhost");
  stateWithoutKey.keep_alive(true);
  const auto stateWithoutKeyResponse = exchange(socket, buffer, stateWithoutKey);
  expect(stateWithoutKeyResponse.result() == http::status::ok && stateWithoutKeyResponse.body() == "room:topic",
         "nested optional route suffix did not match its short form");

  http::request<http::empty_body> stateWithKey{http::verb::get, "/_matrix/client/v3/rooms/room/state/topic/name", 11};
  stateWithKey.set(http::field::host, "localhost");
  stateWithKey.keep_alive(true);
  const auto stateWithKeyResponse = exchange(socket, buffer, stateWithKey);
  expect(stateWithKeyResponse.result() == http::status::ok && stateWithKeyResponse.body() == "room:topic:name",
         "nested optional route suffix did not capture its final parameter");

  net::ip::tcp::socket limitedSocket{io};
  limitedSocket.connect(net::ip::tcp::endpoint{net::ip::address_v4::loopback(), port});
  beast::flat_buffer limitedBuffer;
  http::request<http::string_body> limited{http::verb::post, "/limited", 11};
  limited.set(http::field::host, "localhost");
  limited.body() = "12345";
  limited.prepare_payload();
  const auto limitedResponse = exchange(limitedSocket, limitedBuffer, limited);
  expect(limitedResponse.result() == http::status::payload_too_large,
         "route-specific body limit did not reject declared content length");
  expect(!limitedResponse.keep_alive(), "route-specific body rejection must close the connection");

  net::ip::tcp::socket limitedChunkedSocket{io};
  limitedChunkedSocket.connect(net::ip::tcp::endpoint{net::ip::address_v4::loopback(), port});
  const std::string limitedChunkedRequest =
      "POST /limited HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
      "5\r\n12345\r\n0\r\n\r\n";
  net::write(limitedChunkedSocket, net::buffer(limitedChunkedRequest));
  beast::flat_buffer limitedChunkedBuffer;
  http::response<http::string_body> limitedChunkedResponse;
  http::read(limitedChunkedSocket, limitedChunkedBuffer, limitedChunkedResponse);
  expect(limitedChunkedResponse.result() == http::status::payload_too_large,
         "route-specific body limit did not reject chunked content");

  net::ip::tcp::socket oversizedSocket{io};
  oversizedSocket.connect(net::ip::tcp::endpoint{net::ip::address_v4::loopback(), port});
  beast::flat_buffer oversizedBuffer;
  http::request<http::string_body> oversized{http::verb::post, "/echo", 11};
  oversized.set(http::field::host, "localhost");
  oversized.body() = std::string(64, 'x');
  oversized.prepare_payload();
  auto oversizedResponse = exchange(oversizedSocket, oversizedBuffer, oversized);
  expect(oversizedResponse.result() == http::status::payload_too_large,
         "body limit must produce 413 instead of a generic application error");
  expect(!oversizedResponse.keep_alive(), "body limit response must close the connection");

  net::ip::tcp::socket chunkedSocket{io};
  chunkedSocket.connect(net::ip::tcp::endpoint{net::ip::address_v4::loopback(), port});
  const std::string chunkedRequest =
      "POST /echo HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
      "40\r\n" +
      std::string(64, 'c') + "\r\n0\r\n\r\n";
  net::write(chunkedSocket, net::buffer(chunkedRequest));
  beast::flat_buffer chunkedBuffer;
  http::response<http::string_body> chunkedResponse;
  http::read(chunkedSocket, chunkedBuffer, chunkedResponse);
  expect(chunkedResponse.result() == http::status::payload_too_large,
         "streamed body limit must produce 413 through recovery middleware");

  net::ip::tcp::socket largeHeaderSocket{io};
  largeHeaderSocket.connect(net::ip::tcp::endpoint{net::ip::address_v4::loopback(), port});
  beast::flat_buffer largeHeaderBuffer;
  http::request<http::empty_body> largeHeader{http::verb::get, "/one", 11};
  largeHeader.set(http::field::host, "localhost");
  largeHeader.set("X-Large", std::string(2048, 'h'));
  auto largeHeaderResponse = exchange(largeHeaderSocket, largeHeaderBuffer, largeHeader);
  expect(largeHeaderResponse.result() == http::status::request_header_fields_too_large,
         "header limit must produce 431");

  http::request<http::empty_body> wrongMethod{http::verb::post, "/users/42", 11};
  wrongMethod.set(http::field::host, "localhost");
  wrongMethod.keep_alive(true);
  auto methodResponse = exchange(socket, buffer, wrongMethod);
  expect(methodResponse.result() == http::status::method_not_allowed, "method mismatch must produce 405");
  expect(methodResponse[http::field::allow].find("GET") != beast::string_view::npos, "405 response must include Allow");

  http::request<http::empty_body> preflight{http::verb::options, "/users/42", 11};
  preflight.set(http::field::host, "localhost");
  preflight.set(http::field::origin, "https://example.test");
  preflight.set(http::field::access_control_request_method, "GET");
  preflight.keep_alive(true);
  auto preflightResponse = exchange(socket, buffer, preflight);
  expect(preflightResponse.result() == http::status::no_content, "CORS preflight should be handled");
  expect(preflightResponse[http::field::access_control_allow_origin] == "*", "CORS response origin is missing");

  http::request<http::empty_body> first{http::verb::get, "/one", 11};
  first.set(http::field::host, "localhost");
  first.keep_alive(true);
  http::request<http::empty_body> second{http::verb::get, "/two", 11};
  second.set(http::field::host, "localhost");
  second.keep_alive(false);
  http::write(socket, first);
  http::write(socket, second);
  http::response<http::string_body> firstResponse;
  http::response<http::string_body> secondResponse;
  http::read(socket, buffer, firstResponse);
  http::read(socket, buffer, secondResponse);
  expect(firstResponse.body() == "one" && secondResponse.body() == "two", "pipelined requests lost buffered bytes");

  net::ip::tcp::socket rejectedSocket{io};
  rejectedSocket.connect(net::ip::tcp::endpoint{net::ip::address_v4::loopback(), port});
  beast::flat_buffer rejectedBuffer;
  http::request<http::empty_body> rejected{http::verb::get, "/ws", 11};
  rejected.set(http::field::host, "localhost");
  rejected.keep_alive(false);
  auto rejectedResponse = exchange(rejectedSocket, rejectedBuffer, rejected);
  expect(rejectedResponse.result() == http::status::upgrade_required,
         "ordinary request to a WebSocket endpoint should be rejected with 426");

  websocket::stream<net::ip::tcp::socket> ws{io};
  ws.next_layer().connect(net::ip::tcp::endpoint{net::ip::address_v4::loopback(), port});
  ws.handshake("localhost", "/ws");
  ws.text(true);
  constexpr std::string_view websocketPayload{"hello websocket"};
  ws.write(net::buffer(websocketPayload.data(), websocketPayload.size()));
  beast::flat_buffer websocketBuffer;
  ws.read(websocketBuffer);
  expect(ws.got_text() && beast::buffers_to_string(websocketBuffer.data()) == "hello websocket",
         "plain WebSocket endpoint did not echo a text message");
  websocketBuffer.consume(websocketBuffer.size());
  const std::array<std::byte, 3> websocketBinary{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}};
  ws.binary(true);
  ws.write(net::buffer(websocketBinary));
  ws.read(websocketBuffer);
  std::array<std::byte, 3> echoedBinary{};
  net::buffer_copy(net::buffer(echoedBinary), websocketBuffer.data());
  expect(ws.got_binary() && echoedBinary == websocketBinary,
         "plain WebSocket endpoint did not echo an opaque binary message");
  boost::system::error_code websocketCloseEc;
  ws.close(websocket::close_code::normal, websocketCloseEc);

  net::ip::tcp::socket failureSocket{io};
  failureSocket.connect(net::ip::tcp::endpoint{net::ip::address_v4::loopback(), port});
  beast::flat_buffer failureBuffer;
  http::request<http::empty_body> failure{http::verb::get, "/boom", 11};
  failure.set(http::field::host, "localhost");
  auto failureResponse = exchange(failureSocket, failureBuffer, failure);
  expect(failureResponse.result() == http::status::internal_server_error,
         "recovery middleware should map application exceptions");
  expect(!failureResponse.keep_alive(), "recovered exceptions should close the connection by default");

  for (int i = 0; i < 50 && logCalls.load() < 10; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  expect(logCalls.load() >= 10, "access log middleware did not observe completed requests");
  expect(logged405.load() == 1, "access log middleware must observe automatic response status");
  expect(loggedRouteAndConnection.load(), "access log must include route and connection metadata");
}
