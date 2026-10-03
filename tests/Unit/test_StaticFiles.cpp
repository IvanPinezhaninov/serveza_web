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
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <boost/beast/http.hpp>

#include <gtest/gtest.h>

#include <serveza/web/middleware/static_files.h>
#include <serveza/web/middleware/traffic_observer.h>

#include "RequestHarness.h"

namespace {

namespace http = boost::beast::http;
namespace web = serveza::web;

class TemporaryDirectory final {
public:
  TemporaryDirectory()
  {
    static std::atomic_uint64_t next{};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    m_path = std::filesystem::temp_directory_path() /
             ("serveza-web-static-" + std::to_string(stamp) + '-' + std::to_string(next.fetch_add(1)));
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

void setPathParameter(RequestHarness& request, std::string_view value)
{
  ASSERT_TRUE(web::details::route_params_access::push(request.state().route_values, "path", value));
}

TEST(StaticFilesValidationTest, RequiresAnExistingDirectoryAndParameterName)
{
  TemporaryDirectory temporary;
  EXPECT_THROW(web::middleware::static_files(temporary.path() / "missing"), std::invalid_argument);
  temporary.write("file.txt", "value");
  EXPECT_THROW(web::middleware::static_files(temporary.path() / "file.txt"), std::invalid_argument);
  EXPECT_THROW(web::middleware::static_files(temporary.path(), ""), std::invalid_argument);
}

TEST(StaticFilesValidationTest, RejectsUnsafeHeadersAndIndexNames)
{
  TemporaryDirectory temporary;
  web::middleware::static_files_options options;
  options.cache_control = "public\r\nX-Injected: yes";
  EXPECT_THROW(web::middleware::static_files(temporary.path(), options), std::invalid_argument);

  options.cache_control.clear();
  options.index_files = {"../index.html"};
  EXPECT_THROW(web::middleware::static_files(temporary.path(), options), std::invalid_argument);
}

TEST(StaticFilesConfigurationTest, ExposesNormalizedConfiguration)
{
  TemporaryDirectory temporary;
  web::middleware::static_files_options options;
  options.route_parameter = "asset";
  options.cache_control = "private";
  web::middleware::static_files handler{temporary.path(), options};

  EXPECT_EQ(handler.root(), std::filesystem::canonical(temporary.path()));
  EXPECT_EQ(handler.route_parameter(), "asset");
  EXPECT_EQ(handler.options().cache_control, "private");
}

TEST(StaticFilesResponseTest, SelectsMimeTypeAndReportsTheFileSize)
{
  TemporaryDirectory temporary;
  temporary.write("styles/SITE.CSS", "body { color: red; }");
  web::middleware::static_files handler{temporary.path()};
  RequestHarness request;
  const std::string path{"styles/SITE.CSS"};
  setPathParameter(request, path);

  bool continued{};
  auto continueRequest = [&] { continued = true; };
  auto next = makeTestNext(continueRequest);
  request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, next, yield);
  });

  ASSERT_EQ(request.transport.fileResponseStatus, http::status::ok);
  EXPECT_EQ(request.transport.fileResponseHeaders[http::field::content_type], "text/css; charset=utf-8");
  EXPECT_EQ(request.transport.fileResponseHeaders[http::field::content_length], "20");
  EXPECT_FALSE(request.transport.fileResponseHeaders[http::field::etag].empty());
  EXPECT_FALSE(request.transport.fileResponseHeaders[http::field::last_modified].empty());
  EXPECT_EQ(request.transport.fileResponseSize, 20U);
  EXPECT_FALSE(request.transport.skippedBody);
  EXPECT_FALSE(continued);
}

TEST(StaticFilesResponseTest, ExposesFileMetadataToTrafficObserversWithoutReadingTheFileAgain)
{
  TemporaryDirectory temporary;
  temporary.write("asset.bin", "file contents");
  web::middleware::static_files handler{temporary.path()};
  RequestHarness request;
  const std::string path{"asset.bin"};
  setPathParameter(request, path);

  struct Capture final {
    void operator()(const web::middleware::response_headers_event& event)
    {
      kind = event.body_kind;
      size = event.body_size;
    }

    void operator()(const web::middleware::response_file_event& event)
    {
      file = event.path;
      offset = event.offset;
      size = event.size;
    }

    web::middleware::response_body_kind kind{web::middleware::response_body_kind::empty};
    std::optional<std::uint64_t> size;
    std::filesystem::path file;
    std::uint64_t offset{};
  } observed;
  auto observer = web::middleware::observe_traffic{std::ref(observed)};

  request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    auto missing = [] { FAIL() << "existing file fell through"; };
    auto staticNext = makeTestNext(missing);
    auto serve = [&](boost::asio::yield_context innerYield) {
      serveza::web::async_invoke_layer(handler, ctx, staticNext, innerYield);
    };
    auto observerNext = makeTestNext(serve);
    serveza::web::async_invoke_layer(observer, ctx, observerNext, yield);
  });

  EXPECT_EQ(observed.kind, web::middleware::response_body_kind::file);
  ASSERT_TRUE(observed.size);
  EXPECT_EQ(*observed.size, 13U);
  EXPECT_EQ(observed.file, handler.root() / "asset.bin");
  EXPECT_EQ(observed.offset, 0U);
}

TEST(StaticFilesContentTypeTest, SupportsOverridesCompoundExtensionsAndFallbacks)
{
  web::middleware::mime_type_registry types;
  types.set("css", "application/x-custom-css");
  types.set("archive.tar.gz", "application/x-compound");
  types.set_fallback("application/x-unknown");

  EXPECT_EQ(types.find("SITE.CSS"), "application/x-custom-css");
  EXPECT_EQ(types.find("bundle.archive.tar.gz"), "application/x-compound");
  EXPECT_EQ(types.find("without-extension"), "application/x-unknown");
  EXPECT_TRUE(types.erase(".css"));
  EXPECT_EQ(types.find("SITE.CSS"), "application/x-unknown");
  EXPECT_THROW(types.set("", "text/plain"), std::invalid_argument);
  EXPECT_THROW(types.set("bad/path", "text/plain"), std::invalid_argument);
  EXPECT_THROW(types.set("safe", "text/plain\r\nInjected: yes"), std::invalid_argument);
}

TEST(StaticFilesContentTypeTest, SupportsValueSemantics)
{
  web::middleware::mime_type_registry original;
  original.set("custom", "application/x-original").set_fallback("application/x-fallback");

  web::middleware::mime_type_registry copied{original};
  EXPECT_EQ(copied.find("value.custom"), "application/x-original");
  EXPECT_EQ(copied.fallback(), "application/x-fallback");

  web::middleware::mime_type_registry assigned;
  assigned = original;
  EXPECT_EQ(assigned.find("value.custom"), "application/x-original");

  web::middleware::mime_type_registry moved;
  moved = std::move(assigned);
  EXPECT_EQ(moved.find("value.custom"), "application/x-original");
}

TEST(StaticFilesResponseTest, AppliesCustomContentTypeAndCacheControl)
{
  TemporaryDirectory temporary;
  temporary.write("model.custom", "value");
  web::middleware::static_files_options options;
  options.content_types.set("custom", "application/x-model");
  options.cache_control = "public, max-age=60";
  web::middleware::static_files handler{temporary.path(), std::move(options)};
  RequestHarness request;
  const std::string path{"model.custom"};
  setPathParameter(request, path);

  auto terminal = [] { FAIL() << "existing file fell through"; };
  auto next = makeTestNext(terminal);
  request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, next, yield);
  });

  EXPECT_EQ(request.transport.fileResponseHeaders[http::field::content_type], "application/x-model");
  EXPECT_EQ(request.transport.fileResponseHeaders[http::field::cache_control], "public, max-age=60");
}

TEST(StaticFilesConditionalTest, HonorsETagAndLastModifiedValidators)
{
  TemporaryDirectory temporary;
  temporary.write("asset.txt", "cacheable");
  web::middleware::static_files handler{temporary.path()};
  const std::string path{"asset.txt"};

  RequestHarness initial;
  setPathParameter(initial, path);
  auto terminal = [] { FAIL() << "existing file fell through"; };
  auto initialNext = makeTestNext(terminal);
  initial.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, initialNext, yield);
  });
  const std::string etag{initial.transport.fileResponseHeaders[http::field::etag]};
  const std::string modified{initial.transport.fileResponseHeaders[http::field::last_modified]};
  ASSERT_FALSE(etag.empty());
  ASSERT_FALSE(modified.empty());

  RequestHarness byTag;
  setPathParameter(byTag, path);
  byTag.setRequestHeader(http::field::if_none_match, etag);
  auto tagNext = makeTestNext(terminal);
  byTag.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, tagNext, yield);
  });
  ASSERT_TRUE(byTag.transport.response);
  EXPECT_EQ(byTag.transport.response->result(), http::status::not_modified);
  EXPECT_EQ((*byTag.transport.response)[http::field::etag], etag);
  EXPECT_FALSE(byTag.transport.fileResponseStatus);

  RequestHarness byDate;
  setPathParameter(byDate, path);
  byDate.setRequestHeader(http::field::if_modified_since, modified);
  auto dateNext = makeTestNext(terminal);
  byDate.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, dateNext, yield);
  });
  ASSERT_TRUE(byDate.transport.response);
  EXPECT_EQ(byDate.transport.response->result(), http::status::not_modified);
  EXPECT_EQ((*byDate.transport.response)[http::field::last_modified], modified);
}

TEST(StaticFilesConditionalTest, ETagUsesNativeFileTimestampResolution)
{
  TemporaryDirectory temporary;
  const auto file = temporary.path() / "asset.txt";
  temporary.write("asset.txt", "first");

  using FileTime = std::filesystem::file_time_type;
  const auto second = std::chrono::time_point_cast<std::chrono::seconds>(FileTime::clock::now());
  const auto firstTimestamp = second + std::chrono::milliseconds{100};
  const auto secondTimestamp = second + std::chrono::milliseconds{200};
  std::filesystem::last_write_time(file, firstTimestamp);
  const auto actualFirstTimestamp = std::filesystem::last_write_time(file);

  web::middleware::static_files handler{temporary.path()};
  const std::string path{"asset.txt"};
  const auto readEtag = [&](RequestHarness& request) {
    setPathParameter(request, path);
    auto terminal = [] { FAIL() << "existing file fell through"; };
    auto next = makeTestNext(terminal);
    request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
      serveza::web::async_invoke_layer(handler, ctx, next, yield);
    });
    return std::string{request.transport.fileResponseHeaders[http::field::etag]};
  };

  RequestHarness first;
  const auto firstEtag = readEtag(first);
  temporary.write("asset.txt", "other");
  std::filesystem::last_write_time(file, secondTimestamp);
  if (std::filesystem::last_write_time(file) == actualFirstTimestamp)
    GTEST_SKIP() << "filesystem timestamp is too coarse";
  RequestHarness changed;
  const auto changedEtag = readEtag(changed);

  EXPECT_NE(firstEtag, changedEtag);
  EXPECT_EQ(first.transport.fileResponseHeaders[http::field::last_modified],
            changed.transport.fileResponseHeaders[http::field::last_modified]);
}

TEST(StaticFilesConditionalTest, IfNoneMatchTakesPrecedenceAndInvalidDatesAreIgnored)
{
  TemporaryDirectory temporary;
  temporary.write("asset.txt", "cacheable");
  web::middleware::static_files handler{temporary.path()};
  const std::string path{"asset.txt"};

  for (const std::string_view date : {"Sun, 06 Nov 2094 08:49:37 GMT", "not-a-date"}) {
    RequestHarness request;
    setPathParameter(request, path);
    request.setRequestHeader(http::field::if_none_match, "\"different\"");
    request.setRequestHeader(http::field::if_modified_since, date);
    auto terminal = [] { FAIL() << "existing file fell through"; };
    auto next = makeTestNext(terminal);
    request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
      serveza::web::async_invoke_layer(handler, ctx, next, yield);
    });
    EXPECT_EQ(request.transport.fileResponseStatus, http::status::ok);
  }
}

TEST(StaticFilesConditionalTest, AcceptsLegacyHttpDateFormats)
{
  TemporaryDirectory temporary;
  temporary.write("asset.txt", "cacheable");
  web::middleware::static_files handler{temporary.path()};
  const std::string path{"asset.txt"};

  for (const std::string_view date : {"Sunday, 06-Nov-94 08:49:37 GMT", "Sun Nov  6 08:49:37 2094"}) {
    RequestHarness request;
    setPathParameter(request, path);
    request.setRequestHeader(http::field::if_modified_since, date);
    auto terminal = [] { FAIL() << "conditional file request fell through"; };
    auto next = makeTestNext(terminal);
    request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
      serveza::web::async_invoke_layer(handler, ctx, next, yield);
    });

    if (date.find("2094") != std::string_view::npos) {
      ASSERT_TRUE(request.transport.response);
      EXPECT_EQ(request.transport.response->result(), http::status::not_modified);
    } else {
      EXPECT_EQ(request.transport.fileResponseStatus, http::status::ok);
    }
  }
}

TEST(StaticFilesRangeTest, ServesSingleClosedOpenAndSuffixRanges)
{
  TemporaryDirectory temporary;
  temporary.write("asset.txt", "0123456789");
  web::middleware::static_files handler{temporary.path()};
  const std::string path{"asset.txt"};

  struct Expectation final {
    std::string_view header;
    std::uint64_t offset;
    std::uint64_t length;
    std::string_view contentRange;
  } cases[] = {
      {"bytes=2-5", 2, 4, "bytes 2-5/10"},
      {"bytes=7-", 7, 3, "bytes 7-9/10"},
      {"bytes=-3", 7, 3, "bytes 7-9/10"},
      {"bytes=8-99", 8, 2, "bytes 8-9/10"},
  };

  for (const auto& expected : cases) {
    RequestHarness request;
    setPathParameter(request, path);
    request.setRequestHeader(http::field::range, expected.header);
    auto terminal = [] { FAIL() << "existing file fell through"; };
    auto next = makeTestNext(terminal);
    request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
      serveza::web::async_invoke_layer(handler, ctx, next, yield);
    });

    EXPECT_EQ(request.transport.fileResponseStatus, http::status::partial_content);
    EXPECT_EQ(request.transport.fileResponseOffset, expected.offset);
    EXPECT_EQ(request.transport.fileResponseSize, expected.length);
    EXPECT_EQ(request.transport.fileResponseHeaders[http::field::content_length], std::to_string(expected.length));
    EXPECT_EQ(request.transport.fileResponseHeaders[http::field::content_range], expected.contentRange);
    EXPECT_EQ(request.transport.fileResponseHeaders[http::field::accept_ranges], "bytes");
  }
}

TEST(StaticFilesRangeTest, RejectsUnsatisfiableByteRanges)
{
  TemporaryDirectory temporary;
  temporary.write("asset.txt", "0123456789");
  web::middleware::static_files handler{temporary.path()};
  const std::string path{"asset.txt"};

  for (const std::string_view header : {"bytes=20-", "bytes=5-2", "bytes=-0"}) {
    RequestHarness request;
    setPathParameter(request, path);
    request.setRequestHeader(http::field::range, header);
    auto terminal = [] { FAIL() << "invalid range fell through"; };
    auto next = makeTestNext(terminal);
    request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
      serveza::web::async_invoke_layer(handler, ctx, next, yield);
    });

    ASSERT_TRUE(request.transport.response);
    EXPECT_EQ(request.transport.response->result(), http::status::range_not_satisfiable);
    EXPECT_EQ((*request.transport.response)[http::field::content_range], "bytes */10");
  }
}

TEST(StaticFilesRangeTest, HonorsIfRangeAndIgnoresUnknownRangeUnits)
{
  TemporaryDirectory temporary;
  temporary.write("asset.txt", "0123456789");
  web::middleware::static_files handler{temporary.path()};
  const std::string path{"asset.txt"};

  for (const auto& [range, ifRange] : std::vector<std::pair<std::string_view, std::string_view>>{
           {"items=2-5", {}},
           {"bytes=0-1,4-5", {}},
           {"bytes=invalid", {}},
           {"bytes=2-5", "Wed, 21 Oct 2015 07:28:00 GMT"},
           {"bytes=2-5", "W/\"stale\""},
       }) {
    RequestHarness request;
    setPathParameter(request, path);
    request.setRequestHeader(http::field::range, range);
    if (!ifRange.empty()) request.setRequestHeader(http::field::if_range, ifRange);
    auto terminal = [] { FAIL() << "existing file fell through"; };
    auto next = makeTestNext(terminal);
    request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
      serveza::web::async_invoke_layer(handler, ctx, next, yield);
    });

    EXPECT_EQ(request.transport.fileResponseStatus, http::status::ok);
    EXPECT_EQ(request.transport.fileResponseSize, 10U);
  }
}

TEST(StaticFilesResponseTest, ServesConfiguredDirectoryIndex)
{
  TemporaryDirectory temporary;
  temporary.write("docs/home.html", "home");
  web::middleware::static_files_options options;
  options.index_files = {"home.html"};
  web::middleware::static_files handler{temporary.path(), std::move(options)};
  RequestHarness request;
  const std::string path{"docs/"};
  setPathParameter(request, path);

  auto terminal = [] { FAIL() << "directory index fell through"; };
  auto next = makeTestNext(terminal);
  request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, next, yield);
  });

  EXPECT_EQ(request.transport.fileResponseStatus, http::status::ok);
  EXPECT_EQ(request.transport.fileResponseSize, 4U);
  EXPECT_EQ(request.transport.fileResponseHeaders[http::field::content_type], "text/html; charset=utf-8");
}

TEST(StaticFilesResponseTest, TriesConfiguredIndexFilesInOrder)
{
  TemporaryDirectory temporary;
  temporary.write("docs/home.html", "home");
  web::middleware::static_files_options options;
  options.index_files = {"missing.html", "home.html"};
  web::middleware::static_files handler{temporary.path(), std::move(options)};
  RequestHarness request;
  const std::string path{"docs/"};
  setPathParameter(request, path);

  auto terminal = [] { FAIL() << "second directory index fell through"; };
  auto next = makeTestNext(terminal);
  request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, next, yield);
  });

  EXPECT_EQ(request.transport.fileResponseStatus, http::status::ok);
  EXPECT_EQ(request.transport.fileResponseSize, 4U);
}

TEST(StaticFilesResponseTest, DirectoryWithoutAConfiguredIndexFallsThrough)
{
  TemporaryDirectory temporary;
  std::filesystem::create_directory(temporary.path() / "empty");
  web::middleware::static_files handler{temporary.path()};
  RequestHarness request;
  const std::string path{"empty/"};
  setPathParameter(request, path);

  bool continued{};
  auto continueRequest = [&] { continued = true; };
  auto next = makeTestNext(continueRequest);
  request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, next, yield);
  });

  EXPECT_TRUE(continued);
  EXPECT_FALSE(request.transport.fileResponseStatus);
}

TEST(StaticFilesSecurityTest, HiddenFilesAreDeniedUnlessExplicitlyEnabled)
{
  TemporaryDirectory temporary;
  temporary.write(".well-known/value.txt", "visible by policy");
  const std::string path{".well-known/value.txt"};

  web::middleware::static_files denied{temporary.path()};
  RequestHarness deniedRequest;
  setPathParameter(deniedRequest, path);
  bool continued{};
  auto continueRequest = [&] { continued = true; };
  auto deniedNext = makeTestNext(continueRequest);
  deniedRequest.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(denied, ctx, deniedNext, yield);
  });
  EXPECT_TRUE(continued);

  web::middleware::static_files_options options;
  options.serve_hidden_files = true;
  web::middleware::static_files allowed{temporary.path(), std::move(options)};
  RequestHarness allowedRequest;
  setPathParameter(allowedRequest, path);
  auto terminal = [] { FAIL() << "explicitly allowed hidden file fell through"; };
  auto allowedNext = makeTestNext(terminal);
  allowedRequest.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(allowed, ctx, allowedNext, yield);
  });
  EXPECT_EQ(allowedRequest.transport.fileResponseStatus, http::status::ok);
}

TEST(StaticFilesResponseTest, SendsOnlyHeadersForHead)
{
  TemporaryDirectory temporary;
  temporary.write("manual.pdf", "pdf-data");
  web::middleware::static_files handler{temporary.path()};
  RequestHarness request{http::verb::head};
  const std::string path{"manual.pdf"};
  setPathParameter(request, path);

  bool continued{};
  auto continueRequest = [&] { continued = true; };
  auto next = makeTestNext(continueRequest);
  request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, next, yield);
  });

  ASSERT_EQ(request.transport.fileResponseStatus, http::status::ok);
  EXPECT_EQ(request.transport.fileResponseHeaders[http::field::content_type], "application/pdf");
  EXPECT_EQ(request.transport.fileResponseHeaders[http::field::content_length], "8");
  EXPECT_TRUE(request.transport.skippedBody);
  EXPECT_FALSE(continued);
}

TEST(StaticFilesResponseTest, MissingParameterAndFilesFallThrough)
{
  TemporaryDirectory temporary;
  web::middleware::static_files handler{temporary.path()};

  RequestHarness missingParameter;
  bool missingParameterContinued{};
  auto continueMissingParameter = [&] { missingParameterContinued = true; };
  auto missingParameterNext = makeTestNext(continueMissingParameter);
  missingParameter.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, missingParameterNext, yield);
  });
  EXPECT_TRUE(missingParameterContinued);
  EXPECT_FALSE(missingParameter.transport.response);

  RequestHarness missingFile;
  const std::string path{"missing.txt"};
  setPathParameter(missingFile, path);
  bool missingFileContinued{};
  auto continueMissingFile = [&] { missingFileContinued = true; };
  auto missingFileNext = makeTestNext(continueMissingFile);
  missingFile.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, missingFileNext, yield);
  });
  EXPECT_TRUE(missingFileContinued);
  EXPECT_FALSE(missingFile.transport.response);
}

TEST(StaticFilesResponseTest, MethodsOtherThanGetAndHeadFallThrough)
{
  TemporaryDirectory temporary;
  temporary.write("asset.txt", "value");
  web::middleware::static_files handler{temporary.path()};
  RequestHarness request{http::verb::post};
  const std::string path{"asset.txt"};
  setPathParameter(request, path);

  bool continued{};
  auto continueRequest = [&] { continued = true; };
  auto next = makeTestNext(continueRequest);
  request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, next, yield);
  });

  EXPECT_TRUE(continued);
  EXPECT_FALSE(request.transport.fileResponseStatus);
}

class StaticFilesTraversalTest : public testing::TestWithParam<std::string> {};

TEST_P(StaticFilesTraversalTest, RejectsPathsOutsideTheDocumentRoot)
{
  TemporaryDirectory temporary;
  const auto root = temporary.path() / "public";
  std::filesystem::create_directory(root);
  temporary.write("secret.txt", "secret");
  web::middleware::static_files handler{root};
  RequestHarness request;
  const auto path = GetParam();
  setPathParameter(request, path);

  bool continued{};
  auto continueRequest = [&] { continued = true; };
  auto next = makeTestNext(continueRequest);
  request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, next, yield);
  });

  EXPECT_TRUE(continued);
  EXPECT_FALSE(request.transport.response);
  EXPECT_FALSE(request.transport.fileResponseStatus);
}

INSTANTIATE_TEST_SUITE_P(EncodedAndNative, StaticFilesTraversalTest,
                         testing::Values("../secret.txt", "%2e%2e/secret.txt", "%2e%2e%2fsecret.txt",
                                         "folder/../../secret.txt", "/secret.txt", "folder\\secret.txt",
                                         "folder//secret.txt", "folder/%00secret.txt"));

TEST(StaticFilesTraversalTest, RejectsASymlinkEscapingTheDocumentRoot)
{
  TemporaryDirectory temporary;
  const auto root = temporary.path() / "public";
  std::filesystem::create_directory(root);
  temporary.write("secret.txt", "secret");
  std::error_code ec;
  std::filesystem::create_symlink(temporary.path() / "secret.txt", root / "link.txt", ec);
  if (ec) GTEST_SKIP() << "symlink creation is unavailable: " << ec.message();

  web::middleware::static_files handler{root};
  RequestHarness request;
  const std::string path{"link.txt"};
  setPathParameter(request, path);
  bool continued{};
  auto continueRequest = [&] { continued = true; };
  auto next = makeTestNext(continueRequest);
  request.run([&](web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(handler, ctx, next, yield);
  });

  EXPECT_TRUE(continued);
  EXPECT_FALSE(request.transport.response);
  EXPECT_FALSE(request.transport.fileResponseStatus);
}

} // namespace
