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

#include <cstddef>
#include <stdexcept>
#include <string>

#include <boost/asio/deferred.hpp>
#include <boost/beast/http.hpp>

#include <gtest/gtest.h>

#include <serveza/web/context.h>
#include <serveza/web/cookies.h>
#include <serveza/web/errors.h>

#include "RequestHarness.h"

namespace {

namespace http = boost::beast::http;

TEST(RequestContextTest, ExposesRequestAndConnectionMetadata)
{
  RequestHarness request{http::verb::post, "/items/42?view=full", false};
  request.setRequestHeader("X-Test", "value");

  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context) {
    EXPECT_EQ(ctx.method(), http::verb::post);
    EXPECT_EQ(ctx.version(), 11U);
    EXPECT_FALSE(ctx.keep_alive());
    EXPECT_EQ(ctx.target().path, "/items/42");
    EXPECT_EQ(ctx.target().query.first("view"), "full");
    ASSERT_NE(ctx.connection(), nullptr);
    EXPECT_EQ(ctx.connection()->id, 23U);
    EXPECT_EQ(ctx.listener_id(), 17U);
    EXPECT_EQ(ctx.request_headers()["X-Test"], "value");
    const auto& constCtx = ctx;
    EXPECT_EQ(constCtx.response_headers().begin(), constCtx.response_headers().end());
  });
}

TEST(RequestContextTest, CommitsResponseMetadataAndHeaders)
{
  RequestHarness request;
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    ctx.response_headers().set("X-Test", "response");
    ctx.async_send(http::status::created, "body", "application/test", yield);

    EXPECT_TRUE(ctx.response_committed());
    EXPECT_EQ(ctx.response_status(), http::status::created);
    EXPECT_EQ(ctx.response_body_size(), 4U);
    EXPECT_THROW(ctx.async_send_status(http::status::ok, yield), std::logic_error);
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::created);
  EXPECT_EQ(request.transport.response->body(), "body");
  EXPECT_EQ((*request.transport.response)[http::field::content_type], "application/test");
  EXPECT_EQ((*request.transport.response)["X-Test"], "response");
}

TEST(RequestContextTest, RetainsExplicitContentTypeForDeferredInitiation)
{
  RequestHarness request;
  auto ctx = request.state().make_context();
  auto operation = ctx.async_send(http::status::ok, "body", std::string{"application/deferred"}, boost::asio::deferred);
  bool completed{};

  std::move(operation)([&](boost::system::error_code ec) {
    EXPECT_FALSE(ec);
    completed = true;
  });
  request.io.run();

  EXPECT_TRUE(completed);
  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ((*request.transport.response)[http::field::content_type], "application/deferred");
}

TEST(RequestContextTest, HeadUsesGetMetadataWithoutWritingBodyBytes)
{
  RequestHarness request{http::verb::head};
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    ctx.async_send(http::status::ok, "representation", yield);
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_TRUE(request.transport.skippedBody);
  EXPECT_EQ(request.transport.response->body(), "representation");
  EXPECT_EQ(request.state().response_body_size, 14U);
}

TEST(RequestContextTest, PreservesRepeatedSetCookieHeaders)
{
  RequestHarness request;
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::response_cookie first;
    first.name = "one";
    first.value = "1";
    serveza::web::response_cookie second;
    second.name = "two";
    second.value = "2";
    serveza::web::set_cookie(ctx, first);
    serveza::web::set_cookie(ctx, second);
    ctx.async_send_status(http::status::no_content, yield);
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->base().count(http::field::set_cookie), 2U);
}

TEST(CookieHelpersTest, ReadsCookiesWithoutAddingStateToRequestContext)
{
  RequestHarness request;
  request.setRequestHeader(http::field::cookie, "session=token; theme=dark");
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context) {
    const auto cookies = serveza::web::request_cookies(ctx);
    EXPECT_EQ(cookies.first("session"), "token");
    EXPECT_EQ(cookies.first("theme"), "dark");
  });
}

TEST(RequestContextTest, CloseAfterResponseOverridesKeepAlive)
{
  RequestHarness request;
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    ASSERT_TRUE(ctx.keep_alive());
    ctx.close_after_response();
    EXPECT_FALSE(ctx.keep_alive());
    ctx.async_send_status(http::status::ok, yield);
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_FALSE(request.transport.response->keep_alive());
}

TEST(RequestContextBodyTest, EmptyBodyCanBeStreamedExactlyOnce)
{
  RequestHarness request{http::verb::post};
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    std::size_t calls{};
    ctx.async_read_body_chunks([&](std::string_view) { ++calls; }, yield);

    EXPECT_EQ(calls, 0U);
    EXPECT_TRUE(ctx.body_consumed());
    EXPECT_THROW(ctx.async_read_body_chunks([](std::string_view) {}, yield), std::logic_error);
    EXPECT_THROW((void)ctx.async_read_body(yield), std::logic_error);
    ctx.async_send_status(http::status::no_content, yield);
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::no_content);
}

TEST(RequestContextBodyTest, StreamsBodyIncrementallyAndWithoutBufferingIt)
{
  RequestHarness request{http::verb::post, "/", true, "abcdefgh"};
  request.transport.maximumReadSize = 3;
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    std::string received;
    std::size_t calls{};
    ctx.async_read_body_chunks(
        [&](std::string_view chunk) {
          received.append(chunk);
          ++calls;
        },
        yield);

    EXPECT_EQ(received, "abcdefgh");
    EXPECT_EQ(calls, 3U);
    EXPECT_TRUE(ctx.body_consumed());
    EXPECT_TRUE(request.state().body.empty());
  });
}

TEST(RequestContextBodyTest, BuffersBodyIncrementallyAndCachesTheResult)
{
  RequestHarness request{http::verb::post, "/", true, "buffered"};
  request.transport.maximumReadSize = 2;
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    const auto first = ctx.async_read_body(yield);
    const auto offset = request.transport.bodyOffset;
    const auto second = ctx.async_read_body(yield);

    EXPECT_EQ(first, "buffered");
    EXPECT_EQ(first.data(), second.data());
    EXPECT_EQ(request.transport.bodyOffset, offset);
    EXPECT_TRUE(ctx.body_consumed());
  });
}

TEST(RequestContextBodyTest, ExposesBufferedBodyAsOpaqueBytesWithoutACopy)
{
  const std::string input{"\0\x7f\xff", 3};
  RequestHarness request{http::verb::post, "/", true, input};
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    const auto bytes = ctx.async_read_body_bytes(yield);

    ASSERT_EQ(bytes.size(), 3U);
    EXPECT_EQ(std::to_integer<unsigned>(bytes.begin()[0]), 0U);
    EXPECT_EQ(std::to_integer<unsigned>(bytes.begin()[1]), 0x7fU);
    EXPECT_EQ(std::to_integer<unsigned>(bytes.begin()[2]), 0xffU);
    EXPECT_EQ(static_cast<const void*>(bytes.data()), static_cast<const void*>(ctx.async_read_body(yield).data()));
  });
}

TEST(RequestContextBodyTest, StreamsOpaqueByteChunks)
{
  const std::string input{"a\0b\xff", 4};
  RequestHarness request{http::verb::post, "/", true, input};
  request.transport.maximumReadSize = 2;
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::byte_buffer received;
    ctx.async_read_body_byte_chunks(
        [&](serveza::web::byte_view chunk) { received.insert(received.end(), chunk.begin(), chunk.end()); }, yield);

    ASSERT_EQ(received.size(), 4U);
    EXPECT_EQ(std::to_integer<unsigned>(received[1]), 0U);
    EXPECT_EQ(std::to_integer<unsigned>(received[3]), 0xffU);
  });
}

TEST(RequestContextBodyTest, ExplicitDiscardConsumesBodyWithoutBuffering)
{
  RequestHarness request{http::verb::post, "/", true, "discard-me"};
  request.transport.maximumReadSize = 4;
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    ctx.async_discard_body(yield);
    EXPECT_TRUE(ctx.body_consumed());
    EXPECT_EQ(ctx.request_body_size(), 10U);
    EXPECT_TRUE(request.state().body.empty());
  });
  EXPECT_EQ(request.transport.bodyOffset, request.transport.bodyInput.size());
}

TEST(RequestContextBodyTest, CallbackFailureLeavesRemainingBodyDrainable)
{
  RequestHarness request{http::verb::post, "/", true, "stream-failure"};
  request.transport.maximumReadSize = 3;
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    EXPECT_THROW(
        ctx.async_read_body_chunks([](std::string_view) { throw std::runtime_error{"consumer failed"}; }, yield),
        std::runtime_error);
    EXPECT_FALSE(ctx.body_consumed());
    ctx.async_send_status(http::status::bad_request, yield);
    EXPECT_TRUE(ctx.body_consumed());
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_TRUE(request.transport.response->keep_alive());
}

TEST(RequestContextBodyTest, SendingResponseDrainsUnreadBodyForKeepAlive)
{
  RequestHarness request{http::verb::post, "/", true, "unread"};
  request.transport.maximumReadSize = 2;
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    EXPECT_FALSE(ctx.body_consumed());
    ctx.async_send_status(http::status::no_content, yield);
    EXPECT_TRUE(ctx.body_consumed());
  });

  EXPECT_EQ(request.transport.bodyOffset, request.transport.bodyInput.size());
  ASSERT_TRUE(request.transport.response);
  EXPECT_TRUE(request.transport.response->keep_alive());
}

TEST(RequestContextBodyTest, BufferedBodyCannotBeStreamedAgain)
{
  RequestHarness request{http::verb::post};
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    EXPECT_TRUE(ctx.async_read_body(yield).empty());
    EXPECT_THROW(ctx.async_read_body_chunks([](std::string_view) {}, yield), std::logic_error);
    EXPECT_TRUE(ctx.async_read_body(yield).empty());
  });
}

TEST(RequestContextBodyTest, BodyCannotBeReadAfterResponseCommit)
{
  RequestHarness request{http::verb::post};
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    ctx.async_send_status(http::status::no_content, yield);
    EXPECT_THROW(ctx.async_read_body_chunks([](std::string_view) {}, yield), std::logic_error);
    EXPECT_THROW((void)ctx.async_read_body(yield), std::logic_error);
  });
}

TEST(RequestContextBodyLimitTest, ExposesContentLengthAndApplicationMaximum)
{
  RequestHarness request{http::verb::post, "/upload", true, "payload"};
  request.settings.body_limit = 64;
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context) {
    EXPECT_EQ(ctx.content_length(), 7U);
    EXPECT_EQ(ctx.maximum_body_size(), 64U);
    EXPECT_THROW(ctx.set_body_limit(65), std::invalid_argument);
  });
}

TEST(RequestContextBodyLimitTest, RejectsDeclaredBodyBeforeReadingIt)
{
  RequestHarness request{http::verb::post, "/upload", true, "payload"};
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context) {
    try {
      ctx.set_body_limit(3);
      FAIL() << "declared oversized body was accepted";
    } catch (const serveza::web::request_error& e) {
      EXPECT_EQ(e.status(), http::status::payload_too_large);
    }
  });
  EXPECT_EQ(request.transport.bodyOffset, 0U);
}

TEST(RequestContextBodyLimitTest, CannotChangeLimitAfterReadingStarts)
{
  RequestHarness request{http::verb::post, "/upload", true, "payload"};
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    EXPECT_EQ(ctx.async_read_body(yield), "payload");
    EXPECT_THROW(ctx.set_body_limit(3), std::logic_error);
  });
}

TEST(RequestContextBodyLimitTest, TranslatesParserLimitWhileStreaming)
{
  RequestHarness request{http::verb::post, "/upload", true, "payload"};
  request.transport.readEc = http::error::body_limit;
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    try {
      ctx.async_read_body_chunks([](std::string_view) {}, yield);
      FAIL() << "parser body limit was ignored";
    } catch (const boost::system::system_error& e) {
      EXPECT_EQ(e.code(), http::error::body_limit);
      auto ep = ctx.take_exception();
      ASSERT_TRUE(ep);
      try {
        std::rethrow_exception(ep);
      } catch (const serveza::web::request_error& reqError) {
        EXPECT_EQ(reqError.status(), http::status::payload_too_large);
      }
    }
  });
}

TEST(RequestContextBodyLimitTest, TranslatesParserLimitWhileDiscarding)
{
  RequestHarness request{http::verb::post, "/upload", true, "payload"};
  request.transport.readEc = http::error::body_limit;
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    try {
      ctx.async_discard_body(yield);
      FAIL() << "parser body limit was ignored";
    } catch (const boost::system::system_error& e) {
      EXPECT_EQ(e.code(), http::error::body_limit);
      auto ep = ctx.take_exception();
      ASSERT_TRUE(ep);
      try {
        std::rethrow_exception(ep);
      } catch (const serveza::web::request_error& reqError) {
        EXPECT_EQ(reqError.status(), http::status::payload_too_large);
      }
    }
  });
}

TEST(RequestContextResponseTest, SendsAnOwningByteBuffer)
{
  RequestHarness request;
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    ctx.response_headers().set("X-Binary", "yes");
    serveza::web::byte_buffer body{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}};
    ctx.async_send(http::status::ok, std::move(body), "application/x-binary", yield);
  });

  ASSERT_TRUE(request.transport.byteResponse);
  EXPECT_EQ(request.transport.byteResponse->body().size(), 3U);
  EXPECT_EQ(std::to_integer<unsigned>(request.transport.byteResponse->body()[2]), 0xffU);
  EXPECT_EQ((*request.transport.byteResponse)[http::field::content_type], "application/x-binary");
  EXPECT_EQ((*request.transport.byteResponse)[http::field::content_length], "3");
  EXPECT_EQ((*request.transport.byteResponse)["X-Binary"], "yes");
}

TEST(RequestContextResponseTest, WritesAChunkedResponseWithoutBufferingIt)
{
  RequestHarness request;
  request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    ctx.response_headers().set("X-Stream", "yes");
    ctx.async_send_chunked(http::status::ok, "application/octet-stream",
                           serveza::web::yield_chunk_producer{
                               [](serveza::web::response_writer& writer, boost::asio::yield_context innerYield) {
                                 writer.async_write("first", innerYield);
                                 const serveza::web::byte_buffer second{std::byte{0x00}, std::byte{0xff}};
                                 writer.async_write(serveza::web::byte_view{second}, innerYield);
                                 EXPECT_EQ(writer.bytes_written(), 7U);
                               }},
                           yield);
  });

  ASSERT_TRUE(request.transport.chunkedResponse);
  EXPECT_TRUE(request.transport.chunkedResponse->chunked());
  EXPECT_EQ((*request.transport.chunkedResponse)[http::field::content_type], "application/octet-stream");
  EXPECT_EQ((*request.transport.chunkedResponse)["X-Stream"], "yes");
  ASSERT_EQ(request.transport.chunks.size(), 2U);
  EXPECT_EQ(request.transport.chunks[0].size(), 5U);
  EXPECT_EQ(std::to_integer<unsigned>(request.transport.chunks[1][1]), 0xffU);
  EXPECT_TRUE(request.transport.chunkedFinished);
  EXPECT_EQ(request.state().response_body_size, 7U);
}

TEST(RequestContextResponseTest, HeadDoesNotInvokeAChunkProducer)
{
  RequestHarness request{http::verb::head};
  bool invoked{};
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    ctx.async_send_chunked(http::status::ok, "text/plain",
                           serveza::web::yield_chunk_producer{
                               [&](serveza::web::response_writer&, boost::asio::yield_context) { invoked = true; }},
                           yield);
  });

  EXPECT_FALSE(invoked);
  EXPECT_TRUE(request.transport.skippedBody);
  EXPECT_TRUE(request.transport.chunks.empty());
  EXPECT_FALSE(request.transport.chunkedFinished);
  EXPECT_EQ(request.state().response, serveza::web::details::response_progress::completed);
}

class ChunkedResponseStatusTest : public testing::TestWithParam<http::status> {};

TEST_P(ChunkedResponseStatusTest, RejectsStatusesThatCannotCarryABody)
{
  RequestHarness request;
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    EXPECT_THROW(ctx.async_send_chunked(GetParam(), "text/plain",
                                        serveza::web::yield_chunk_producer{
                                            [](serveza::web::response_writer&, boost::asio::yield_context) {}},
                                        yield),
                 std::invalid_argument);
  });

  EXPECT_EQ(request.state().response, serveza::web::details::response_progress::idle);
}

INSTANTIATE_TEST_SUITE_P(BodylessStatuses, ChunkedResponseStatusTest,
                         testing::Values(http::status::continue_, http::status::no_content,
                                         http::status::not_modified));

TEST(RequestContextResponseTest, ProducerFailureClosesACommittedChunkedResponse)
{
  RequestHarness request;

  EXPECT_THROW(request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    ctx.async_send_chunked(
        http::status::ok, "text/plain",
        serveza::web::yield_chunk_producer{[](serveza::web::response_writer&, boost::asio::yield_context) {
          throw std::runtime_error{"producer failed"};
        }},
        yield);
  }),
               std::runtime_error);
  EXPECT_EQ(request.state().response, serveza::web::details::response_progress::started);
  EXPECT_TRUE(request.state().force_close);
  EXPECT_FALSE(request.transport.chunkedFinished);
}

TEST(RequestContextResponseTest, BufferedWriteFailureStillCommitsTheResponse)
{
  RequestHarness request;
  request.transport.writeFailure = TestTransport::WriteFailure::buffered;

  EXPECT_THROW(request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    ctx.async_send(http::status::created, "body", yield);
  }),
               std::runtime_error);

  EXPECT_EQ(request.state().response, serveza::web::details::response_progress::started);
  EXPECT_EQ(request.state().response_status, http::status::created);
  EXPECT_EQ(request.state().response_body_size, 4U);
}

TEST(RequestContextResponseTest, ChunkedHeaderFailureStillCommitsTheResponse)
{
  RequestHarness request;
  request.transport.writeFailure = TestTransport::WriteFailure::chunkedHeader;

  EXPECT_THROW(request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    ctx.async_send_chunked(
        http::status::ok, "text/plain",
        serveza::web::yield_chunk_producer{[](serveza::web::response_writer&, boost::asio::yield_context) {}}, yield);
  }),
               std::runtime_error);

  EXPECT_EQ(request.state().response, serveza::web::details::response_progress::started);
  EXPECT_EQ(request.state().response_status, http::status::ok);
}

TEST(RequestContextResponseTest, ChunkWriteFailureKeepsTheResponseIncompleteAndForcesClose)
{
  RequestHarness request;
  request.transport.writeFailure = TestTransport::WriteFailure::chunk;

  EXPECT_THROW(request.run([](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    ctx.async_send_chunked(http::status::ok, "text/plain",
                           serveza::web::yield_chunk_producer{
                               [](serveza::web::response_writer& writer, boost::asio::yield_context innerYield) {
                                 writer.async_write("chunk", innerYield);
                               }},
                           yield);
  }),
               std::runtime_error);

  EXPECT_EQ(request.state().response, serveza::web::details::response_progress::started);
  EXPECT_TRUE(request.state().force_close);
}

} // namespace
