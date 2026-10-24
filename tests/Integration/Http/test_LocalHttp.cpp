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
#include <filesystem>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <gtest/gtest.h>

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace local = boost::asio::local;
namespace net = boost::asio;
namespace sys = boost::system;
namespace web = serveza::web;
namespace websocket = beast::websocket;

class LocalSocketPath final {
public:
  LocalSocketPath()
  {
    static std::atomic_uint64_t sequence{};
    const auto value = sequence.fetch_add(1, std::memory_order_relaxed);
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    m_path = std::filesystem::temp_directory_path() /
             ("serveza-web-" + std::to_string(timestamp) + '-' + std::to_string(value) + ".sock");
    remove();
  }

  ~LocalSocketPath()
  {
    remove();
  }

  LocalSocketPath(const LocalSocketPath&) = delete;
  LocalSocketPath& operator=(const LocalSocketPath&) = delete;

  [[nodiscard]] std::string string() const
  {
    return m_path.string();
  }

private:
  void remove() noexcept
  {
    std::error_code ignored;
    std::filesystem::remove(m_path, ignored);
  }

  std::filesystem::path m_path;
};

class LocalServerHarness final {
public:
  template<typename SessionFactory>
  LocalServerHarness(std::string path, SessionFactory&& factory)
    : m_path{std::move(path)}
    , m_pool{2}
    , m_server{m_pool.get_executor()}
  {
    m_listener = m_server.listen<local::stream_protocol>(local::stream_protocol::endpoint{m_path},
                                                         std::forward<SessionFactory>(factory));
    std::promise<sys::error_code> ready;
    auto future = ready.get_future();
    m_server.async_start([&ready](sys::error_code ec) { ready.set_value(ec); });
    const sys::error_code ec = future.get();
    if (ec) throw sys::system_error{ec};
  }

  LocalServerHarness(const LocalServerHarness&) = delete;
  LocalServerHarness& operator=(const LocalServerHarness&) = delete;

  ~LocalServerHarness()
  {
    stopNoexcept();
  }

  void stop()
  {
    if (m_stopped) return;
    std::promise<sys::error_code> stopped;
    auto future = stopped.get_future();
    m_server.async_wait([&stopped](sys::error_code ec) { stopped.set_value(ec); });
    m_server.request_stop();
    const sys::error_code ec = future.get();
    m_pool.join();
    m_stopped = true;
    if (ec) throw sys::system_error{ec};
  }

  [[nodiscard]] const std::string& path() const noexcept
  {
    return m_path;
  }

private:
  void stopNoexcept() noexcept
  {
    try {
      stop();
    } catch (...) {}
  }

  std::string m_path;
  net::thread_pool m_pool;
  serveza::server m_server;
  std::shared_ptr<serveza::listener> m_listener;
  bool m_stopped{};
};

web::application makeHttpApplication(std::atomic_bool& sawConnection)
{
  web::router routes;
  routes.get("/health", web::yield_layer{[&](web::request_context& ctx, net::yield_context yield) {
               const serveza::connection_info* connection = ctx.connection();
               sawConnection.store(connection && connection->id != 0 && !connection->local_endpoint.empty(),
                                   std::memory_order_relaxed);
               ctx.async_send(http::status::ok, "healthy", yield);
             }});
  return std::move(routes).build();
}

TEST(LocalHttpIntegrationTest, RejectsEmptyApplication)
{
  EXPECT_THROW(web::local_http_session{web::application{}}, std::invalid_argument);
}

TEST(LocalHttpIntegrationTest, ServesKeepAliveRequests)
{
  std::atomic_bool sawConnection{};
  const web::application app = makeHttpApplication(sawConnection);
  LocalSocketPath socketPath;
  LocalServerHarness server{socketPath.string(), [app] { return web::local_http_session{app}; }};

  net::io_context io;
  local::stream_protocol::socket socket{io};
  socket.connect(local::stream_protocol::endpoint{server.path()});
  beast::flat_buffer buffer;

  http::request<http::empty_body> first{http::verb::get, "/health", 11};
  first.set(http::field::host, "localhost");
  first.keep_alive(true);
  http::write(socket, first);
  http::response<http::string_body> firstResponse;
  http::read(socket, buffer, firstResponse);
  EXPECT_EQ(firstResponse.result(), http::status::ok);
  EXPECT_EQ(firstResponse.body(), "healthy");
  EXPECT_TRUE(firstResponse.keep_alive());

  http::request<http::empty_body> second{http::verb::get, "/health", 11};
  second.set(http::field::host, "localhost");
  second.keep_alive(false);
  http::write(socket, second);
  http::response<http::string_body> secondResponse;
  http::read(socket, buffer, secondResponse);
  EXPECT_EQ(secondResponse.result(), http::status::ok);
  EXPECT_EQ(secondResponse.body(), "healthy");
  EXPECT_FALSE(secondResponse.keep_alive());
  EXPECT_TRUE(sawConnection.load(std::memory_order_relaxed));
}

TEST(LocalHttpIntegrationTest, UpgradesToWebSocket)
{
  web::router routes;
  routes.get("/echo", web::middleware::websocket_upgrade{web::yield_websocket_endpoint{
                          [](web::websocket_connection& connection, net::yield_context yield) {
                            const auto message = connection.async_read(yield);
                            if (message && message->is_text())
                              connection.async_write_text(std::string{message->text()}, yield);
                          }}});
  const web::application app = std::move(routes).build();
  LocalSocketPath socketPath;
  LocalServerHarness server{socketPath.string(), [app] { return web::local_http_session{app}; }};

  net::io_context io;
  websocket::stream<local::stream_protocol::socket> socket{io};
  socket.next_layer().connect(local::stream_protocol::endpoint{server.path()});
  socket.handshake("localhost", "/echo");
  socket.write(net::buffer("hello", 5));
  beast::flat_buffer buffer;
  socket.read(buffer);
  EXPECT_TRUE(socket.got_text());
  EXPECT_EQ(beast::buffers_to_string(buffer.data()), "hello");
  sys::error_code ignored;
  socket.close(websocket::close_code::normal, ignored);
}

} // namespace

#else

TEST(LocalHttpIntegrationTest, LocalSocketsUnavailable)
{
  GTEST_SKIP() << "Boost.Asio local sockets are unavailable on this platform";
}

#endif
