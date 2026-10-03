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

#include <serveza/web/websocket.h>

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/spawn.hpp>

#include <gtest/gtest.h>

#include "detail/core.h"

namespace {

namespace web = serveza::web;

class FakeWebSocket final : public web::details::websocket_io {
public:
  explicit FakeWebSocket(boost::asio::io_context& io)
    : m_io{io}
  {}

  bool is_open() const noexcept override
  {
    return open;
  }

  boost::asio::any_io_executor get_executor() override
  {
    return m_io.get_executor();
  }

  void async_read(read_handler handler) override
  {
    boost::asio::post(m_io, [handler = std::move(handler), message = incoming]() mutable {
      handler(boost::system::error_code{}, std::move(message));
    });
  }

  web::websocket_send_result try_send_text(std::string value) override
  {
    sentText = std::move(value);
    return sendResult;
  }

  web::websocket_send_result try_send_binary(web::byte_buffer value) override
  {
    sentBinary = std::move(value);
    return sendResult;
  }

  void async_write_text(std::string value, web::completion_handler handler) override
  {
    writtenText = std::move(value);
    boost::asio::post(m_io, [handler = std::move(handler)]() mutable { handler(boost::system::error_code{}); });
  }

  void async_write_binary(web::byte_buffer value, web::completion_handler handler) override
  {
    writtenBinary = std::move(value);
    boost::asio::post(m_io, [handler = std::move(handler)]() mutable { handler(boost::system::error_code{}); });
  }

  void async_ping(std::string value, web::completion_handler handler) override
  {
    pingPayload = std::move(value);
    boost::asio::post(m_io, [handler = std::move(handler)]() mutable { handler(boost::system::error_code{}); });
  }

  void async_close(std::uint16_t code, std::string reason, web::completion_handler handler) override
  {
    closeCode = code;
    closeReason = std::move(reason);
    open = false;
    boost::asio::post(m_io, [handler = std::move(handler)]() mutable { handler(boost::system::error_code{}); });
  }

  boost::asio::io_context& m_io;
  bool open{true};
  web::websocket_send_result sendResult{web::websocket_send_result::queued};
  std::optional<web::websocket_message> incoming;
  std::string sentText;
  web::byte_buffer sentBinary;
  std::string writtenText;
  web::byte_buffer writtenBinary;
  std::string pingPayload;
  std::uint16_t closeCode{};
  std::string closeReason;
};

} // namespace

TEST(WebSocketMessageTest, ExposesTextAndBinaryPayloadViews)
{
  web::websocket_message text{{std::byte{'h'}, std::byte{'i'}}, web::websocket_message_type::text};
  EXPECT_TRUE(text.is_text());
  EXPECT_EQ(text.text(), "hi");
  EXPECT_EQ(text.bytes().size(), 2U);

  web::websocket_message binary{{std::byte{0x00}, std::byte{0xff}}, web::websocket_message_type::binary};
  EXPECT_FALSE(binary.is_text());
  EXPECT_EQ(binary.bytes().size(), 2U);
  EXPECT_THROW(static_cast<void>(binary.text()), std::logic_error);

  web::websocket_message empty{{}, web::websocket_message_type::text};
  EXPECT_TRUE(empty.text().empty());
}

TEST(WebSocketOptionsTest, RejectsZeroLimitsAndNonPositiveTimeouts)
{
  const auto expectInvalid = [](auto change) {
    web::websocket_options options;
    change(options);
    EXPECT_THROW(web::details::validate_websocket_options(options), std::invalid_argument);
  };

  expectInvalid([](auto& options) { options.max_message_size = 0; });
  expectInvalid([](auto& options) { options.handshake_timeout = std::chrono::seconds::zero(); });
  expectInvalid([](auto& options) { options.idle_timeout = std::chrono::seconds{-1}; });
  expectInvalid([](auto& options) { options.close_timeout = std::chrono::seconds::zero(); });
  expectInvalid([](auto& options) { options.max_pending_messages = 0; });
  expectInvalid([](auto& options) { options.max_pending_bytes = 0; });
  EXPECT_NO_THROW(web::details::validate_websocket_options(web::websocket_options{}));
}

TEST(WebSocketSenderTest, DefaultSenderIsClosedAndCopyable)
{
  static_assert(std::is_copy_constructible_v<web::websocket_sender>);
  static_assert(std::is_copy_assignable_v<web::websocket_sender>);

  web::websocket_sender sender;
  EXPECT_FALSE(sender.is_open());
  EXPECT_EQ(sender.try_send_text("ignored"), web::websocket_send_result::closed);
  EXPECT_EQ(sender.try_send_binary({std::byte{0x01}}), web::websocket_send_result::closed);
}

TEST(WebSocketConnectionTest, DelegatesOperationsAndCreatesALiveSender)
{
  boost::asio::io_context ctx;
  auto io = std::make_shared<FakeWebSocket>(ctx);
  io->incoming = web::websocket_message{{std::byte{'o'}, std::byte{'k'}}, web::websocket_message_type::text};
  auto connection = web::details::websocket_connection_access::make(io);

  EXPECT_TRUE(connection->is_open());
  auto sender = connection->sender();
  EXPECT_TRUE(sender.is_open());
  EXPECT_EQ(sender.try_send_text("queued"), web::websocket_send_result::queued);
  EXPECT_EQ(io->sentText, "queued");
  EXPECT_EQ(sender.try_send_binary({std::byte{0x2a}}), web::websocket_send_result::queued);
  ASSERT_EQ(io->sentBinary.size(), 1U);
  EXPECT_EQ(io->sentBinary.front(), std::byte{0x2a});

  std::exception_ptr ec;
  boost::asio::spawn(
      ctx,
      [&](boost::asio::yield_context yield) {
        const auto message = connection->async_read(yield);
        ASSERT_TRUE(message);
        EXPECT_EQ(message->text(), "ok");

        connection->async_write_text("written", yield);
        connection->async_write_binary(web::byte_buffer{std::byte{0x01}, std::byte{0x02}}, yield);
        connection->async_ping("alive", yield);
        connection->async_close(1001, "leaving", yield);
      },
      [&](std::exception_ptr value) { ec = std::move(value); });
  ctx.run();
  if (ec) std::rethrow_exception(ec);

  EXPECT_EQ(io->writtenText, "written");
  EXPECT_EQ(io->writtenBinary, (web::byte_buffer{std::byte{0x01}, std::byte{0x02}}));
  EXPECT_EQ(io->pingPayload, "alive");
  EXPECT_EQ(io->closeCode, 1001U);
  EXPECT_EQ(io->closeReason, "leaving");
  EXPECT_FALSE(connection->is_open());
}

TEST(WebSocketConnectionTest, DefaultCloseUsesNormalClosureCode)
{
  boost::asio::io_context ctx;
  auto io = std::make_shared<FakeWebSocket>(ctx);
  auto connection = web::details::websocket_connection_access::make(io);
  std::exception_ptr ec;
  boost::asio::spawn(
      ctx, [&](boost::asio::yield_context yield) { connection->async_close(yield); },
      [&](std::exception_ptr value) { ec = std::move(value); });
  ctx.run();
  if (ec) std::rethrow_exception(ec);

  EXPECT_EQ(io->closeCode, 1000U);
  EXPECT_TRUE(io->closeReason.empty());
}
