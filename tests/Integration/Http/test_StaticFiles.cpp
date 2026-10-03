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

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <gtest/gtest.h>

#include <serveza/serveza.h>
#include <serveza/web.h>
#include <serveza/web/yield.h>

#include "ServerHarness.h"

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace web = serveza::web;

class TemporaryDirectory final {
public:
  TemporaryDirectory()
  {
    static std::atomic_uint64_t next{};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    m_path = std::filesystem::temp_directory_path() /
             ("serveza-web-static-integration-" + std::to_string(stamp) + '-' + std::to_string(next.fetch_add(1)));
    std::filesystem::create_directories(m_path);
  }

  ~TemporaryDirectory()
  {
    std::error_code ignored;
    std::filesystem::remove_all(m_path, ignored);
  }

  const std::filesystem::path& path() const noexcept
  {
    return m_path;
  }

  void write(std::string_view relative, std::string_view contents) const
  {
    const auto target = m_path / std::filesystem::u8path(relative);
    std::filesystem::create_directories(target.parent_path());
    std::ofstream stream{target, std::ios::binary};
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream) throw std::runtime_error{"could not create static file fixture"};
  }

private:
  std::filesystem::path m_path;
};

web::application makeStaticApplication(const std::filesystem::path& root)
{
  web::router routes;
  routes.get("/assets/*path", web::middleware::static_files{root});
  routes.get("/health", web::yield_layer{[](web::request_context& ctx, net::yield_context yield) {
               ctx.async_send(http::status::ok, "healthy", yield);
             }});
  return std::move(routes).build();
}

class StaticHttpServer final {
public:
  explicit StaticHttpServer(web::application application)
    : m_server{[application = std::move(application)] { return web::http_session{application}; }}
  {}

  StaticHttpServer(const StaticHttpServer&) = delete;

  StaticHttpServer& operator=(const StaticHttpServer&) = delete;

  [[nodiscard]] net::ip::tcp::endpoint endpoint() const
  {
    return {net::ip::address_v4::loopback(), m_server.port()};
  }

  void stop()
  {
    m_server.stop();
  }

private:
  ServerHarness m_server;
};

template<typename Body>
http::response<http::string_body> exchange(net::ip::tcp::socket& socket, beast::flat_buffer& buffer,
                                           http::request<Body>& request)
{
  http::write(socket, request);
  http::response<http::string_body> response;
  http::read(socket, buffer, response);
  return response;
}

TEST(StaticFilesIntegrationTest, StreamsALargeFileAndPreservesKeepAlive)
{
  TemporaryDirectory root;
  std::string contents;
  contents.reserve(128U * 1024U);
  for (std::size_t i = 0; i < 128U * 1024U; ++i)
    contents.push_back(static_cast<char>('a' + i % 26U));
  root.write("nested/large.txt", contents);
  StaticHttpServer server{makeStaticApplication(root.path())};

  net::io_context io;
  net::ip::tcp::socket socket{io};
  socket.connect(server.endpoint());
  beast::flat_buffer buffer;
  http::request<http::empty_body> fileRequest{http::verb::get, "/assets/nested/large.txt", 11};
  fileRequest.set(http::field::host, "localhost");
  fileRequest.keep_alive(true);
  const auto fileResponse = exchange(socket, buffer, fileRequest);
  ASSERT_EQ(fileResponse.result(), http::status::ok);
  EXPECT_EQ(fileResponse[http::field::content_type], "text/plain; charset=utf-8");
  EXPECT_EQ(fileResponse.body(), contents);
  EXPECT_TRUE(fileResponse.keep_alive());

  http::request<http::empty_body> healthRequest{http::verb::get, "/health", 11};
  healthRequest.set(http::field::host, "localhost");
  healthRequest.keep_alive(false);
  const auto healthResponse = exchange(socket, buffer, healthRequest);
  EXPECT_EQ(healthResponse.result(), http::status::ok);
  EXPECT_EQ(healthResponse.body(), "healthy");
}

TEST(StaticFilesIntegrationTest, HeadSendsMetadataWithoutFileBytes)
{
  TemporaryDirectory root;
  root.write("manual.pdf", "eight123");
  StaticHttpServer server{makeStaticApplication(root.path())};

  net::io_context io;
  net::ip::tcp::socket socket{io};
  socket.connect(server.endpoint());
  http::request<http::empty_body> request{http::verb::head, "/assets/manual.pdf", 11};
  request.set(http::field::host, "localhost");
  request.keep_alive(false);
  http::write(socket, request);

  beast::flat_buffer buffer;
  http::response_parser<http::string_body> parser;
  parser.skip(true);
  http::read(socket, buffer, parser);
  const auto response = parser.release();
  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response[http::field::content_type], "application/pdf");
  EXPECT_EQ(response[http::field::content_length], "8");
  EXPECT_TRUE(response.body().empty());
}

TEST(StaticFilesIntegrationTest, StreamsASelectedByteRange)
{
  TemporaryDirectory root;
  root.write("manual.txt", "0123456789");
  StaticHttpServer server{makeStaticApplication(root.path())};

  net::io_context io;
  net::ip::tcp::socket socket{io};
  socket.connect(server.endpoint());
  beast::flat_buffer buffer;
  http::request<http::empty_body> request{http::verb::get, "/assets/manual.txt", 11};
  request.set(http::field::host, "localhost");
  request.set(http::field::range, "bytes=2-5");
  request.keep_alive(false);
  const auto response = exchange(socket, buffer, request);

  EXPECT_EQ(response.result(), http::status::partial_content);
  EXPECT_EQ(response[http::field::content_range], "bytes 2-5/10");
  EXPECT_EQ(response[http::field::content_length], "4");
  EXPECT_EQ(response[http::field::accept_ranges], "bytes");
  EXPECT_EQ(response.body(), "2345");
}

TEST(StaticFilesIntegrationTest, UnsafeAndMissingPathsFallThroughToNotFound)
{
  TemporaryDirectory parent;
  const auto root = parent.path() / "public";
  std::filesystem::create_directory(root);
  parent.write("secret.txt", "secret");
  StaticHttpServer server{makeStaticApplication(root)};

  net::io_context io;
  for (const std::string_view target : {"/assets/%2e%2e%2fsecret.txt", "/assets/missing.txt"}) {
    net::ip::tcp::socket socket{io};
    socket.connect(server.endpoint());
    beast::flat_buffer buffer;
    http::request<http::empty_body> request{http::verb::get, target, 11};
    request.set(http::field::host, "localhost");
    request.keep_alive(false);
    const auto response = exchange(socket, buffer, request);
    EXPECT_EQ(response.result(), http::status::not_found) << target;
    EXPECT_TRUE(response.body().empty()) << target;
  }
}

TEST(StaticFilesIntegrationTest, ReturnsNotModifiedForCurrentValidators)
{
  TemporaryDirectory root;
  root.write("cached.js", "const value = 42;");
  StaticHttpServer server{makeStaticApplication(root.path())};

  net::io_context io;
  net::ip::tcp::socket socket{io};
  socket.connect(server.endpoint());
  beast::flat_buffer buffer;
  http::request<http::empty_body> initial{http::verb::get, "/assets/cached.js", 11};
  initial.set(http::field::host, "localhost");
  initial.keep_alive(true);
  const auto initialResponse = exchange(socket, buffer, initial);
  ASSERT_EQ(initialResponse.result(), http::status::ok);
  const std::string etag{initialResponse[http::field::etag]};
  const std::string modified{initialResponse[http::field::last_modified]};
  ASSERT_FALSE(etag.empty());
  ASSERT_FALSE(modified.empty());

  http::request<http::empty_body> conditional{http::verb::get, "/assets/cached.js", 11};
  conditional.set(http::field::host, "localhost");
  conditional.set(http::field::if_none_match, etag);
  conditional.set(http::field::if_modified_since, modified);
  conditional.keep_alive(false);
  const auto conditionalResponse = exchange(socket, buffer, conditional);
  EXPECT_EQ(conditionalResponse.result(), http::status::not_modified);
  EXPECT_EQ(conditionalResponse[http::field::etag], etag);
  EXPECT_TRUE(conditionalResponse.body().empty());
}

} // namespace
