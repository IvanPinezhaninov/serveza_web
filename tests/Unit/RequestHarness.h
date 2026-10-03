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

#ifndef SERVEZA_WEB_TEST_REQUEST_HARNESS_H
#define SERVEZA_WEB_TEST_REQUEST_HARNESS_H

#include <algorithm>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>

#include <serveza/connection_info.h>

#include <serveza/web/context.h>
#include <serveza/web/settings.h>
#include <serveza/web/yield.h>

#include "detail/core.h"

inline thread_local boost::asio::io_context* requestHarnessIoContext{};

class TestTransport final : public serveza::web::details::transport {
public:
  explicit TestTransport(boost::asio::io_context& io)
    : m_io{io}
    , m_completionExecutor{io.get_executor()}
  {}

  enum class WriteFailure {
    none,
    buffered,
    file,
    chunkedHeader,
    chunk,
    chunkLast,
  };

  [[nodiscard]] boost::asio::any_io_executor get_executor() override
  {
    return m_io.get_executor();
  }

  [[nodiscard]] const boost::asio::any_completion_executor& get_completion_executor() const noexcept override
  {
    return m_completionExecutor;
  }

  void async_read_header(boost::beast::flat_buffer&, serveza::web::details::parser_type&,
                         serveza::web::details::completion_callback) override
  {
    throw std::logic_error{"unexpected header read"};
  }

  void async_read_some(boost::beast::flat_buffer&, serveza::web::details::parser_type& parser,
                       serveza::web::details::completion_callback handler) override
  {
    if (readEc) return complete(std::move(handler), *readEc);
    if (bodyOffset >= bodyInput.size()) throw std::logic_error{"unexpected body read"};

    const auto available = bodyInput.size() - bodyOffset;
    const auto size = std::min(available, maximumReadSize);
    boost::system::error_code ec;
    const auto consumed = parser.put(boost::asio::buffer(bodyInput.data() + bodyOffset, size), ec);
    bodyOffset += consumed;
    if (ec == boost::beast::http::error::need_more || ec == boost::beast::http::error::need_buffer) ec.clear();
    if (ec) return complete(std::move(handler), ec);
    if (consumed == 0 && !parser.is_done()) throw std::logic_error{"body parser made no progress"};
    complete(std::move(handler), {});
  }

  void async_write(serveza::web::details::string_response& value, bool skipBody,
                   serveza::web::details::completion_callback handler) override
  {
    response = value;
    skippedBody = skipBody;
    fail(WriteFailure::buffered);
    complete(std::move(handler), {});
  }

  void async_write(serveza::web::details::byte_response& value, bool skipBody,
                   serveza::web::details::completion_callback handler) override
  {
    byteResponse = value;
    skippedBody = skipBody;
    fail(WriteFailure::buffered);
    complete(std::move(handler), {});
  }

  void async_write(serveza::web::details::file_response& value, bool skipBody,
                   serveza::web::details::completion_callback handler) override
  {
    fileResponseStatus = value.result();
    fileResponseHeaders = value.base();
    fileResponseSize = value.body().size();
    skippedBody = skipBody;
    fail(WriteFailure::file);
    complete(std::move(handler), {});
  }

  void async_write_file_range(serveza::web::details::empty_response& value, boost::beast::file& file,
                              std::uint64_t length, bool skipBody,
                              serveza::web::details::completion_callback handler) override
  {
    boost::system::error_code ec;
    fileResponseOffset = file.pos(ec);
    if (ec) throw boost::system::system_error{ec};
    fileResponseStatus = value.result();
    fileResponseHeaders = value.base();
    fileResponseSize = length;
    skippedBody = skipBody;
    fail(WriteFailure::file);
    complete(std::move(handler), {});
  }

  void async_write_chunked_header(serveza::web::details::empty_response& value, bool skipBody,
                                  serveza::web::details::completion_callback handler) override
  {
    chunkedResponse = value;
    skippedBody = skipBody;
    fail(WriteFailure::chunkedHeader);
    complete(std::move(handler), {});
  }

  void async_write_chunk(const void* data, std::size_t size,
                         serveza::web::details::completion_callback handler) override
  {
    fail(WriteFailure::chunk);
    const auto* first = static_cast<const std::byte*>(data);
    chunks.emplace_back(first, first + size);
    complete(std::move(handler), {});
  }

  void async_write_chunk_last(serveza::web::details::completion_callback handler) override
  {
    fail(WriteFailure::chunkLast);
    chunkedFinished = true;
    complete(std::move(handler), {});
  }

  void async_accept_websocket(const serveza::web::details::parser_type&, const boost::beast::http::fields&,
                              const serveza::web::websocket_options&, void*, websocket_callback,
                              serveza::web::details::completion_callback) override
  {
    throw std::logic_error{"unexpected WebSocket upgrade"};
  }

  std::optional<serveza::web::details::string_response> response;
  std::optional<serveza::web::details::byte_response> byteResponse;
  std::optional<serveza::web::details::empty_response> chunkedResponse;
  std::vector<serveza::web::byte_buffer> chunks;
  bool chunkedFinished{};
  std::optional<boost::beast::http::status> fileResponseStatus;
  boost::beast::http::fields fileResponseHeaders;
  std::uint64_t fileResponseSize{};
  std::uint64_t fileResponseOffset{};
  bool skippedBody{};
  std::string bodyInput;
  std::size_t bodyOffset{};
  std::size_t maximumReadSize{std::numeric_limits<std::size_t>::max()};
  std::optional<boost::system::error_code> readEc;
  WriteFailure writeFailure{WriteFailure::none};

private:
  void complete(serveza::web::details::completion_callback handler, boost::system::error_code ec)
  {
    boost::asio::post(m_io, [handler = std::move(handler), ec]() mutable { handler(ec); });
  }

  void fail(WriteFailure point) const
  {
    if (writeFailure == point) throw std::runtime_error{"simulated transport write failure"};
  }

  boost::asio::io_context& m_io;
  boost::asio::any_completion_executor m_completionExecutor;
};

class RequestHarness final {
public:
  RequestHarness(boost::beast::http::verb method = boost::beast::http::verb::get, std::string target = "/",
                 bool keepAlive = true, std::string body = {})
    : transport{io}
    , m_targetText{std::move(target)}
    , m_target{serveza::web::details::parse_target(m_targetText)}
    , m_state{transport, buffer, parser, m_target, settings, 17U, &connection}
  {
    const auto methodText = boost::beast::http::to_string(method);
    std::string request{methodText.data(), methodText.size()};
    request += " / HTTP/1.1\r\nHost: example.test\r\n";
    if (!body.empty()) request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    request += "\r\n";
    boost::system::error_code ec;
    parser.put(boost::asio::buffer(request), ec);
    if (ec == boost::beast::http::error::need_more) ec.clear();
    if (ec || (body.empty() && !parser.is_done())) throw std::runtime_error{"could not prepare test request"};
    transport.bodyInput = std::move(body);
    parser.get().method(method);
    parser.get().target(m_targetText);
    parser.get().keep_alive(keepAlive);
    connection.id = 23U;
    connection.local_endpoint = "127.0.0.1:8080";
    connection.remote_endpoint = "127.0.0.1:40000";
  }

  void setRequestHeader(boost::beast::http::field field, std::string_view value)
  {
    parser.get().set(field, boost::beast::string_view{value.data(), value.size()});
  }

  void setRequestHeader(std::string_view name, std::string_view value)
  {
    parser.get().set(boost::beast::string_view{name.data(), name.size()},
                     boost::beast::string_view{value.data(), value.size()});
  }

  template<typename Callable>
  void run(Callable&& callable)
  {
    std::exception_ptr ec;
    boost::asio::spawn(
        io,
        [&](boost::asio::yield_context yield) {
          auto ctx = m_state.make_context();
          callable(ctx, yield);
        },
        [&](std::exception_ptr value) { ec = std::move(value); });
    io.restart();
    requestHarnessIoContext = &io;
    io.run();
    requestHarnessIoContext = nullptr;
    if (ec) std::rethrow_exception(ec);
  }

  serveza::web::details::request_state& state() noexcept
  {
    return m_state;
  }

  boost::asio::io_context io;
  TestTransport transport;
  boost::beast::flat_buffer buffer;
  serveza::web::details::parser_type parser;
  serveza::web::settings settings;
  serveza::connection_info connection;

private:
  std::string m_targetText;
  serveza::web::target_view m_target;
  serveza::web::details::request_state m_state;
};

template<typename Callable>
class TestNext final {
public:
  explicit TestNext(Callable& callable)
    : m_state{&callable, nullptr}
    , m_next{serveza::web::details::continuation_access::make(&m_state, &invoke)}
  {}

  TestNext(Callable& callable, serveza::web::request_context& ctx)
    : m_state{&callable, &ctx}
    , m_next{serveza::web::details::continuation_access::make(&m_state, &invoke)}
  {}

  operator serveza::web::continuation&() noexcept
  {
    return m_next;
  }

private:
  struct State final {
    Callable* callable;
    serveza::web::request_context* ctx;
  };

  static void invoke(void* value, serveza::web::completion_handler handler)
  {
    auto& state = *static_cast<State*>(value);
    const auto executor = state.ctx ? state.ctx->get_executor() : requestHarnessIoContext->get_executor();
    if constexpr (std::is_invocable_v<Callable&, boost::asio::yield_context>) {
      boost::asio::post(executor, [&state, executor, handler = std::move(handler)]() mutable {
        boost::asio::spawn(
            executor, [&state](boost::asio::yield_context yield) { (*state.callable)(yield); },
            [&state, handler = std::move(handler)](std::exception_ptr ep) mutable {
              if (state.ctx) state.ctx->report_exception(ep);
              handler(serveza::web::details::layer_exception_to_error(std::move(ep)));
            });
      });
    } else {
      boost::asio::post(executor, [&state, handler = std::move(handler)]() mutable {
        boost::system::error_code ec;
        try {
          (*state.callable)();
        } catch (...) {
          auto ep = std::current_exception();
          if (state.ctx) state.ctx->report_exception(ep);
          ec = serveza::web::details::layer_exception_to_error(std::move(ep));
        }
        handler(ec);
      });
    }
  }

  State m_state;
  serveza::web::continuation m_next;
};

template<typename Callable>
TestNext<Callable> makeTestNext(Callable& callable)
{
  return TestNext<Callable>{callable};
}

template<typename Callable>
TestNext<Callable> makeTestNext(Callable& callable, serveza::web::request_context& ctx)
{
  return TestNext<Callable>{callable, ctx};
}

#endif // SERVEZA_WEB_TEST_REQUEST_HARNESS_H
