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

#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_set>

#include <boost/beast/http.hpp>

#include <gtest/gtest.h>

#include <serveza/web/errors.h>
#include <serveza/web/middleware/access_log.h>
#include <serveza/web/middleware/cors.h>
#include <serveza/web/middleware/recover.h>
#include <serveza/web/middleware/request_body_limit.h>
#include <serveza/web/middleware/request_id.h>
#include <serveza/web/middleware/security_headers.h>
#include <serveza/web/middleware/traffic_observer.h>
#include <serveza/web/middleware/websocket_upgrade.h>

#include "RequestHarness.h"

namespace {

namespace http = boost::beast::http;
namespace middleware = serveza::web::middleware;

bool isUuidV4(std::string_view value)
{
  if (value.size() != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-') return false;
  const auto isHex = [](char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
  };
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (index == 8 || index == 13 || index == 18 || index == 23) continue;
    if (!isHex(value[index])) return false;
  }
  return value[14] == '4' && (value[19] == '8' || value[19] == '9' || value[19] == 'a' || value[19] == 'b');
}

struct MoveOnlyAccessSink final {
  explicit MoveOnlyAccessSink(int& callCount)
    : m_token{std::make_unique<int>(1)}
    , m_calls{&callCount}
  {}

  void operator()(const middleware::access_log_entry&)
  {
    ++*m_calls;
  }

  std::unique_ptr<int> m_token;
  int* m_calls;
};

struct MoveOnlyLimitSelector final {
  explicit MoveOnlyLimitSelector(std::size_t value)
    : m_limit{std::make_unique<std::size_t>(value)}
  {}

  std::size_t operator()(const serveza::web::request_context&) const noexcept
  {
    return *m_limit;
  }

  std::unique_ptr<std::size_t> m_limit;
};

struct MoveOnlyExceptionObserver final {
  explicit MoveOnlyExceptionObserver(bool& observedFlag)
    : m_token{std::make_unique<int>(1)}
    , m_observed{&observedFlag}
  {}

  void operator()(std::exception_ptr)
  {
    *m_observed = true;
  }

  std::unique_ptr<int> m_token;
  bool* m_observed;
};

struct MoveOnlyWebSocketEndpoint final {
  MoveOnlyWebSocketEndpoint()
    : m_token{std::make_unique<int>(1)}
  {}

  void operator()(serveza::web::websocket_connection&, boost::asio::yield_context) {}

  std::unique_ptr<int> m_token;
};

struct TrafficCapture final {
  TrafficCapture(int& requestHeaders, int& responseHeaders, int& completedCount, std::string& requestBody,
                 std::string& responseBody)
    : m_requestHeaders{&requestHeaders}
    , m_responseHeaders{&responseHeaders}
    , m_completed{&completedCount}
    , m_requestBody{&requestBody}
    , m_responseBody{&responseBody}
  {}

  void operator()(const middleware::request_headers_event& event)
  {
    ++*m_requestHeaders;
    m_requestMethod = event.context.method();
    m_exchange = event.exchange;
  }

  void operator()(const middleware::request_body_chunk_event& event)
  {
    m_sameExchange = m_sameExchange && m_exchange == event.exchange;
    m_requestBody->append(reinterpret_cast<const char*>(event.chunk.data()), event.chunk.size());
  }

  void operator()(const middleware::response_headers_event& event)
  {
    ++*m_responseHeaders;
    m_sameExchange = m_sameExchange && m_exchange == event.exchange;
    m_responseStatus = event.status;
    m_responseKind = event.body_kind;
    m_responseContentType = std::string{event.headers[http::field::content_type]};
  }

  void operator()(const middleware::response_body_chunk_event& event)
  {
    m_sameExchange = m_sameExchange && m_exchange == event.exchange;
    m_responseBody->append(reinterpret_cast<const char*>(event.chunk.data()), event.chunk.size());
  }

  void operator()(const middleware::exchange_complete_event& event)
  {
    ++*m_completed;
    m_sameExchange = m_sameExchange && m_exchange == event.exchange;
    m_completionMethod = event.method;
    m_completionRoute = event.route;
    m_completionStatus = event.status;
    m_completionRequestBodySize = event.request_body_size;
    m_completionResponseBodySize = event.response_body_size;
    m_completionResponseCompleted = event.response_completed;
    m_completionElapsed = event.elapsed;
    m_completionFailed = static_cast<bool>(event.error);
  }

  int* m_requestHeaders;
  int* m_responseHeaders;
  int* m_completed;
  std::string* m_requestBody;
  std::string* m_responseBody;
  http::verb m_requestMethod{http::verb::unknown};
  http::status m_responseStatus{http::status::unknown};
  middleware::response_body_kind m_responseKind{middleware::response_body_kind::empty};
  std::string m_responseContentType;
  const void* m_exchange{};
  http::verb m_completionMethod{http::verb::unknown};
  std::string m_completionRoute;
  std::optional<http::status> m_completionStatus;
  std::size_t m_completionRequestBodySize{};
  std::size_t m_completionResponseBodySize{};
  std::chrono::microseconds m_completionElapsed{};
  bool m_sameExchange{true};
  bool m_completionFailed{};
  bool m_completionResponseCompleted{};
};

TEST(RequestIdMiddlewareTest, GeneratesAndReturnsARequestId)
{
  middleware::request_id middleware;
  RequestHarness request;
  std::string generated;
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [&](boost::asio::yield_context innerYield) {
      const auto* value = ctx.storage().get<middleware::request_id_value>();
      ASSERT_NE(value, nullptr);
      generated = value->value;
      ctx.async_send_status(http::status::ok, innerYield);
    };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_TRUE(isUuidV4(generated));
  EXPECT_EQ((*request.transport.response)["X-Request-ID"], generated);
}

TEST(RequestIdMiddlewareTest, GeneratesUniqueValuesAcrossMiddlewareInstances)
{
  middleware::request_id first;
  middleware::request_id second;
  std::unordered_set<std::string> generated;

  for (std::size_t index = 0; index < 256; ++index) {
    auto& selected = index % 2 == 0 ? first : second;
    RequestHarness request;
    std::string value;
    request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
      auto terminal = [&] { value = ctx.storage().require<middleware::request_id_value>().value; };
      auto next = makeTestNext(terminal, ctx);
      serveza::web::async_invoke_layer(selected, ctx, next, yield);
    });
    EXPECT_TRUE(isUuidV4(value));
    EXPECT_TRUE(generated.emplace(std::move(value)).second);
  }
}

TEST(RequestIdMiddlewareTest, TrustsOnlyValidBoundedIncomingValues)
{
  middleware::request_id_options options;
  options.trust_incoming = true;
  options.max_incoming_length = 8U;
  middleware::request_id middleware{options};

  RequestHarness trusted;
  trusted.setRequestHeader("X-Request-ID", "client-1");
  trusted.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] {};
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });
  EXPECT_EQ(trusted.state().response_fields["X-Request-ID"], "client-1");

  RequestHarness rejected;
  rejected.setRequestHeader("X-Request-ID", "invalid value");
  rejected.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] {};
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });
  const auto rejectedValue = rejected.state().response_fields["X-Request-ID"];
  EXPECT_NE(rejectedValue, "invalid value");
  EXPECT_TRUE(isUuidV4(std::string_view{rejectedValue.data(), rejectedValue.size()}));

  RequestHarness invalidCharacter;
  invalidCharacter.setRequestHeader("X-Request-ID", "bad$id");
  invalidCharacter.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] {};
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });
  EXPECT_NE(invalidCharacter.state().response_fields["X-Request-ID"], "bad$id");
}

TEST(RecoverMiddlewareTest, MapsApplicationAndRequestErrors)
{
  bool observed{};
  middleware::recover_options options;
  options.status = http::status::service_unavailable;
  options.body = "temporarily unavailable";
  middleware::recover middleware{options, [&](std::exception_ptr) { observed = true; }};
  RequestHarness applicationFailure;
  applicationFailure.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] { throw std::runtime_error{"failure"}; };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });
  ASSERT_TRUE(applicationFailure.transport.response);
  EXPECT_TRUE(observed);
  EXPECT_EQ(applicationFailure.transport.response->result(), http::status::service_unavailable);
  EXPECT_EQ(applicationFailure.transport.response->body(), "temporarily unavailable");
  EXPECT_FALSE(applicationFailure.transport.response->keep_alive());

  RequestHarness requestFailure;
  requestFailure.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] { throw serveza::web::request_error{http::status::bad_request, "bad input"}; };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });
  ASSERT_TRUE(requestFailure.transport.response);
  EXPECT_EQ(requestFailure.transport.response->result(), http::status::bad_request);
  EXPECT_EQ(requestFailure.transport.response->body(), "bad input");
}

TEST(RecoverMiddlewareTest, IgnoresFailuresFromTheExceptionObserver)
{
  middleware::recover middleware{middleware::recover_options{},
                                 [](std::exception_ptr) { throw std::runtime_error{"observer failed"}; }};
  RequestHarness request;
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] { throw std::runtime_error{"handler failed"}; };
    auto next = makeTestNext(terminal, ctx);
    EXPECT_NO_THROW(serveza::web::async_invoke_layer(middleware, ctx, next, yield));
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::internal_server_error);
}

TEST(SecurityHeadersMiddlewareTest, AppliesConfiguredHeadersBeforeNext)
{
  middleware::security_headers_options options;
  options.content_security_policy = "default-src 'self'";
  options.permissions_policy = "camera=()";
  options.strict_transport_security = "max-age=31536000";
  middleware::security_headers middleware{options};
  RequestHarness request;
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [&](boost::asio::yield_context innerYield) { ctx.async_send_status(http::status::ok, innerYield); };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ((*request.transport.response)["X-Content-Type-Options"], "nosniff");
  EXPECT_EQ((*request.transport.response)["X-Frame-Options"], "DENY");
  EXPECT_EQ((*request.transport.response)["Referrer-Policy"], "no-referrer");
  EXPECT_EQ((*request.transport.response)["Content-Security-Policy"], "default-src 'self'");
  EXPECT_EQ((*request.transport.response)["Permissions-Policy"], "camera=()");
  EXPECT_EQ((*request.transport.response)["Strict-Transport-Security"], "max-age=31536000");
}

TEST(CorsMiddlewareTest, ContinuesWithoutHeadersForDisallowedOrigin)
{
  middleware::cors_options options;
  options.allowed_origins = {"https://allowed.test"};
  middleware::cors middleware{options};
  RequestHarness request;
  request.setRequestHeader(http::field::origin, "https://other.test");
  bool continued{};
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [&] { continued = true; };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });

  EXPECT_TRUE(continued);
  EXPECT_EQ(request.state().response_fields.count("Access-Control-Allow-Origin"), 0U);
}

TEST(CorsMiddlewareTest, HandlesCredentialedPreflight)
{
  middleware::cors_options options;
  options.allow_any_origin = true;
  options.allow_credentials = true;
  options.allowed_headers = "Content-Type, Authorization";
  options.max_age = std::chrono::seconds{600};
  middleware::cors middleware{options};
  RequestHarness request{http::verb::options};
  request.setRequestHeader(http::field::origin, "https://client.test");
  request.setRequestHeader(http::field::access_control_request_method, "POST");
  request.setRequestHeader(http::field::access_control_request_headers, "content-type, AUTHORIZATION");
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] { FAIL() << "preflight must terminate the chain"; };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::no_content);
  EXPECT_EQ((*request.transport.response)["Access-Control-Allow-Origin"], "https://client.test");
  EXPECT_EQ((*request.transport.response)["Access-Control-Allow-Credentials"], "true");
  EXPECT_EQ((*request.transport.response)["Access-Control-Allow-Headers"], "Content-Type, Authorization");
  EXPECT_EQ((*request.transport.response)["Access-Control-Max-Age"], "600");
  EXPECT_EQ(request.transport.response->base().count(http::field::vary), 3U);
}

TEST(CorsMiddlewareTest, RejectsDisallowedPreflightMethod)
{
  middleware::cors_options options;
  options.allow_any_origin = true;
  options.allowed_methods = "GET, HEAD";
  middleware::cors middleware{options};
  RequestHarness request{http::verb::options};
  request.setRequestHeader(http::field::origin, "https://client.test");
  request.setRequestHeader(http::field::access_control_request_method, "DELETE");
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] { FAIL() << "rejected preflight must terminate the chain"; };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::forbidden);
}

TEST(CorsMiddlewareTest, AddsExposedHeadersToAnOrdinaryAllowedRequest)
{
  middleware::cors_options options;
  options.allow_any_origin = true;
  options.exposed_headers = "X-Request-ID";
  middleware::cors middleware{options};
  RequestHarness request;
  request.setRequestHeader(http::field::origin, "https://client.test");
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [&](boost::asio::yield_context innerYield) { ctx.async_send_status(http::status::ok, innerYield); };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ((*request.transport.response)["Access-Control-Allow-Origin"], "*");
  EXPECT_EQ((*request.transport.response)["Access-Control-Expose-Headers"], "X-Request-ID");
}

TEST(CorsMiddlewareTest, RejectsDisallowedOrMalformedPreflightHeaders)
{
  middleware::cors_options options;
  options.allow_any_origin = true;
  options.allowed_headers = "Content-Type, X-Request-ID";
  middleware::cors middleware{options};

  for (const std::string_view requested : {"Authorization", "Content-Type,", "bad header"}) {
    RequestHarness request{http::verb::options};
    request.setRequestHeader(http::field::origin, "https://client.test");
    request.setRequestHeader(http::field::access_control_request_method, "POST");
    request.setRequestHeader(http::field::access_control_request_headers, requested);
    request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
      auto terminal = [] { FAIL() << "rejected preflight must terminate the chain"; };
      auto next = makeTestNext(terminal, ctx);
      serveza::web::async_invoke_layer(middleware, ctx, next, yield);
    });

    ASSERT_TRUE(request.transport.response);
    EXPECT_EQ(request.transport.response->result(), http::status::forbidden);
  }
}

TEST(CorsMiddlewareTest, ReflectsWildcardHeadersForCredentialedRequests)
{
  middleware::cors_options options;
  options.allow_any_origin = true;
  options.allow_credentials = true;
  options.allowed_headers = "*";
  middleware::cors middleware{options};
  RequestHarness request{http::verb::options};
  request.setRequestHeader(http::field::origin, "https://client.test");
  request.setRequestHeader(http::field::access_control_request_method, "POST");
  request.setRequestHeader(http::field::access_control_request_headers, "X-Custom, Authorization");
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] { FAIL() << "preflight must terminate the chain"; };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::no_content);
  EXPECT_EQ((*request.transport.response)["Access-Control-Allow-Headers"], "X-Custom, Authorization");
}

TEST(CorsMiddlewareTest, RejectsInvalidConfiguration)
{
  middleware::cors_options options;
  options.allowed_methods.clear();
  EXPECT_THROW(middleware::cors{options}, std::invalid_argument);

  options = {};
  options.allowed_headers = "Content-Type,";
  EXPECT_THROW(middleware::cors{options}, std::invalid_argument);

  options = {};
  options.exposed_headers = "bad header";
  EXPECT_THROW(middleware::cors{options}, std::invalid_argument);

  options = {};
  options.allowed_origins = {"https://example.test\r\nInjected: yes"};
  EXPECT_THROW(middleware::cors{options}, std::invalid_argument);

  options = {};
  options.max_age = std::chrono::seconds{-1};
  EXPECT_THROW(middleware::cors{options}, std::invalid_argument);
}

TEST(CorsMiddlewareTest, TrimsAllowedMethodTokensOnBothSides)
{
  middleware::cors_options options;
  options.allow_any_origin = true;
  options.allowed_methods = "GET , POST";
  middleware::cors middleware{options};
  RequestHarness request{http::verb::options};
  request.setRequestHeader(http::field::origin, "https://client.test");
  request.setRequestHeader(http::field::access_control_request_method, "GET");
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] { FAIL() << "preflight must terminate the chain"; };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::no_content);
}

TEST(AccessLogMiddlewareTest, EmitsResponseAndConnectionMetadata)
{
  std::optional<middleware::access_log_entry> captured;
  middleware::access_log middleware{[&](const auto& entry) { captured = entry; }};
  RequestHarness request{http::verb::get, "/users/42"};
  request.state().current_route = "/users/:id";
  request.state().request_storage.emplace<middleware::request_id_value>(middleware::request_id_value{"request-7"});
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [&](boost::asio::yield_context innerYield) {
      ctx.async_send(http::status::ok, "answer", innerYield);
    };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
    EXPECT_FALSE(captured);
    serveza::web::details::complete_traffic_observers(request.state(), nullptr);
  });

  ASSERT_TRUE(captured);
  EXPECT_EQ(captured->method, http::verb::get);
  EXPECT_EQ(captured->path, "/users/42");
  EXPECT_EQ(captured->route, "/users/:id");
  EXPECT_EQ(captured->status, http::status::ok);
  EXPECT_EQ(captured->request_body_size, 0U);
  EXPECT_EQ(captured->response_body_size, 6U);
  EXPECT_TRUE(captured->response_completed);
  EXPECT_EQ(captured->request_id, "request-7");
  EXPECT_EQ(captured->listener_id, 17U);
  EXPECT_EQ(captured->connection_id, 23U);
  EXPECT_EQ(captured->remote_endpoint, "127.0.0.1:40000");
  EXPECT_FALSE(captured->error);
}

TEST(AccessLogMiddlewareTest, ObservesTheFinalErrorResponse)
{
  std::optional<middleware::access_log_entry> captured;
  middleware::access_log middleware{[&](const auto& entry) { captured = entry; }};
  RequestHarness request;
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] { throw std::runtime_error{"handler failed"}; };
    auto next = makeTestNext(terminal, ctx);
    std::exception_ptr failure;
    try {
      serveza::web::async_invoke_layer(middleware, ctx, next, yield);
    } catch (...) {
      failure = std::current_exception();
    }

    EXPECT_FALSE(captured);
    ctx.async_send(http::status::internal_server_error, "error", yield);
    serveza::web::details::complete_traffic_observers(request.state(), failure);
  });

  ASSERT_TRUE(captured);
  EXPECT_EQ(captured->status, http::status::internal_server_error);
  EXPECT_EQ(captured->response_body_size, 5U);
  EXPECT_TRUE(captured->response_completed);
  EXPECT_TRUE(captured->error);
}

TEST(TrafficObserverMiddlewareTest, ExposesHeadersAndBufferedBodiesWithoutCopies)
{
  int requestHeaders{};
  int responseHeaders{};
  int completed{};
  std::string requestBody;
  std::string responseBody;
  TrafficCapture capture{requestHeaders, responseHeaders, completed, requestBody, responseBody};
  auto observer = middleware::observe_traffic{std::ref(capture)};
  RequestHarness request{http::verb::post, "/echo", true, "request data"};
  request.state().current_route = "/echo";
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [&](boost::asio::yield_context innerYield) {
      EXPECT_EQ(ctx.async_read_body(innerYield), "request data");
      ctx.async_send(http::status::created, "response data", "application/test", innerYield);
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
    };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(observer, ctx, next, yield);
    serveza::web::details::complete_traffic_observers(request.state(), nullptr);
  });

  EXPECT_EQ(requestHeaders, 1);
  EXPECT_EQ(responseHeaders, 1);
  EXPECT_EQ(completed, 1);
  EXPECT_EQ(requestBody, "request data");
  EXPECT_EQ(responseBody, "response data");
  EXPECT_EQ(capture.m_requestMethod, http::verb::post);
  EXPECT_EQ(capture.m_responseStatus, http::status::created);
  EXPECT_EQ(capture.m_responseKind, middleware::response_body_kind::buffered);
  EXPECT_EQ(capture.m_responseContentType, "application/test");
  EXPECT_TRUE(capture.m_sameExchange);
  EXPECT_EQ(capture.m_completionMethod, http::verb::post);
  EXPECT_EQ(capture.m_completionRoute, "/echo");
  EXPECT_EQ(capture.m_completionStatus, http::status::created);
  EXPECT_EQ(capture.m_completionRequestBodySize, 12U);
  EXPECT_EQ(capture.m_completionResponseBodySize, 13U);
  EXPECT_TRUE(capture.m_completionResponseCompleted);
  EXPECT_GE(capture.m_completionElapsed, std::chrono::milliseconds{1});
  EXPECT_FALSE(capture.m_completionFailed);
}

TEST(TrafficObserverMiddlewareTest, ReportsAnIncompleteTransportWrite)
{
  bool captured{};
  std::optional<http::status> status;
  std::size_t responseBodySize{};
  bool responseCompleted{true};
  bool failed{};
  auto observer = middleware::observe_traffic{[&](const middleware::exchange_complete_event& event) {
    captured = true;
    status = event.status;
    responseBodySize = event.response_body_size;
    responseCompleted = event.response_completed;
    failed = static_cast<bool>(event.error);
  }};
  RequestHarness request;
  request.transport.writeFailure = TestTransport::WriteFailure::buffered;
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [&](boost::asio::yield_context innerYield) {
      ctx.async_send(http::status::ok, "response", innerYield);
    };
    auto next = makeTestNext(terminal, ctx);
    std::exception_ptr failure;
    try {
      serveza::web::async_invoke_layer(observer, ctx, next, yield);
    } catch (...) {
      failure = std::current_exception();
    }
    serveza::web::details::complete_traffic_observers(request.state(), failure);
  });

  EXPECT_TRUE(captured);
  EXPECT_EQ(status, http::status::ok);
  EXPECT_EQ(responseBodySize, 8U);
  EXPECT_FALSE(responseCompleted);
  EXPECT_TRUE(failed);
}

TEST(TrafficObserverMiddlewareTest, ObservesChunkedResponsesAndIgnoresSinkFailures)
{
  std::string responseBody;
  struct ThrowingCapture final {
    void operator()(const middleware::request_headers_event&) const
    {
      throw std::runtime_error{"logging failed"};
    }

    void operator()(const middleware::response_body_chunk_event& event) const
    {
      body->append(reinterpret_cast<const char*>(event.chunk.data()), event.chunk.size());
    }

    std::string* body;
  } capture{&responseBody};

  auto observer = middleware::observe_traffic{std::ref(capture)};
  RequestHarness request;
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [&](boost::asio::yield_context terminalYield) {
      ctx.async_send_chunked(http::status::ok, "text/plain",
                             serveza::web::yield_chunk_producer{
                                 [](serveza::web::response_writer& writer, boost::asio::yield_context innerYield) {
                                   writer.async_write("first", innerYield);
                                   writer.async_write("-second", innerYield);
                                 }},
                             terminalYield);
    };
    auto next = makeTestNext(terminal, ctx);
    EXPECT_NO_THROW(serveza::web::async_invoke_layer(observer, ctx, next, yield));
  });

  EXPECT_EQ(responseBody, "first-second");
}

TEST(WebSocketUpgradeMiddlewareTest, RejectsOrdinaryHttpRequests)
{
  auto middleware = middleware::websocket_upgrade{
      serveza::web::yield_websocket_endpoint{[](serveza::web::websocket_connection&, boost::asio::yield_context) {}}};
  RequestHarness request;
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    serveza::web::async_invoke_layer(middleware, ctx, yield);
  });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::upgrade_required);
  EXPECT_EQ((*request.transport.response)[http::field::upgrade], "websocket");
}

TEST(WebSocketUpgradeMiddlewareTest, RejectsInvalidOptionsAtConstruction)
{
  serveza::web::websocket_options options;
  options.max_message_size = 0;
  const auto endpoint =
      serveza::web::yield_websocket_endpoint{[](serveza::web::websocket_connection&, boost::asio::yield_context) {}};

  EXPECT_THROW((middleware::websocket_upgrade{endpoint, options}), std::invalid_argument);
}

TEST(RequestBodyLimitMiddlewareTest, AppliesFixedLimitBeforeBodyRead)
{
  middleware::request_body_limit middleware{4U};
  RequestHarness request{http::verb::post, "/upload", true, "data"};
  bool continued{};
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [&](boost::asio::yield_context innerYield) {
      continued = true;
      EXPECT_EQ(ctx.async_read_body(innerYield), "data");
    };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });
  EXPECT_TRUE(continued);
}

TEST(RequestBodyLimitMiddlewareTest, RejectsDeclaredOversizedBodyBeforeContinuation)
{
  middleware::request_body_limit middleware{4U};
  RequestHarness request{http::verb::post, "/upload", true, "too-large"};
  request.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] { FAIL() << "oversized request continued"; };
    auto next = makeTestNext(terminal, ctx);
    try {
      serveza::web::async_invoke_layer(middleware, ctx, next, yield);
      FAIL() << "oversized request did not throw";
    } catch (const serveza::web::request_error& e) {
      EXPECT_EQ(e.status(), http::status::payload_too_large);
    }
  });
  EXPECT_EQ(request.transport.bodyOffset, 0U);
}

TEST(RequestBodyLimitMiddlewareTest, SelectsLimitFromRequestMetadata)
{
  middleware::request_body_limit_by middleware{
      [](const serveza::web::request_context& ctx) { return ctx.target().path == "/media/upload" ? 32U : 4U; }};

  RequestHarness media{http::verb::post, "/media/upload", true, "larger-media-body"};
  media.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [&](boost::asio::yield_context innerYield) {
      EXPECT_EQ(ctx.async_read_body(innerYield), "larger-media-body");
    };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(middleware, ctx, next, yield);
  });

  RequestHarness api{http::verb::post, "/api", true, "larger-api-body"};
  api.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] { FAIL() << "ordinary API body exceeded its selected limit"; };
    auto next = makeTestNext(terminal, ctx);
    EXPECT_THROW(serveza::web::async_invoke_layer(middleware, ctx, next, yield), serveza::web::request_error);
  });
}

TEST(RequestBodyLimitMiddlewareTest, RejectsNullSelector)
{
  using Selector = std::size_t (*)(const serveza::web::request_context&);
  EXPECT_THROW(middleware::request_body_limit_by{static_cast<Selector>(nullptr)}, std::invalid_argument);
}

TEST(TypedCallableMiddlewareTest, StoresMoveOnlyCallablesDirectly)
{
  int logCalls{};
  auto accessLog = middleware::access_log{MoveOnlyAccessSink{logCalls}};
  RequestHarness logged;
  logged.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [&](boost::asio::yield_context innerYield) { ctx.async_send_status(http::status::ok, innerYield); };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(accessLog, ctx, next, yield);
    serveza::web::details::complete_traffic_observers(logged.state(), nullptr);
  });
  EXPECT_EQ(logCalls, 1);

  auto bodyLimit = middleware::request_body_limit_by{MoveOnlyLimitSelector{4U}};
  RequestHarness limited{http::verb::post, "/upload", true, "data"};
  limited.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [&](boost::asio::yield_context innerYield) { EXPECT_EQ(ctx.async_read_body(innerYield), "data"); };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(bodyLimit, ctx, next, yield);
  });

  bool observed{};
  auto recover = middleware::recover{middleware::recover_options{}, MoveOnlyExceptionObserver{observed}};
  RequestHarness failed;
  failed.run([&](serveza::web::request_context& ctx, boost::asio::yield_context yield) {
    auto terminal = [] { throw std::runtime_error{"failure"}; };
    auto next = makeTestNext(terminal, ctx);
    serveza::web::async_invoke_layer(recover, ctx, next, yield);
  });
  EXPECT_TRUE(observed);

  auto websocket = middleware::websocket_upgrade{serveza::web::yield_websocket_endpoint{MoveOnlyWebSocketEndpoint{}}};
  static_assert(!std::is_copy_constructible_v<decltype(websocket)>);
}

} // namespace
