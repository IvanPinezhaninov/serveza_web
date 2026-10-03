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
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>

#include <gtest/gtest.h>

#include <serveza/web/router.h>
#include <serveza/web/yield.h>

#include "RequestHarness.h"
#include "detail/route_pattern.h"

namespace {

using Context = serveza::web::request_context;
using Yield = boost::asio::yield_context;

auto noOpHandler()
{
  return serveza::web::yield_layer{[](Context&, Yield) {}};
}

TEST(RouterBuilderTest, BuildsAnImmutableCheaplyCopyableApplication)
{
  serveza::web::router routes;
  routes.get("/users/:id", noOpHandler());
  auto application = std::move(routes).build();

  EXPECT_TRUE(application);
  static_assert(std::is_copy_constructible_v<serveza::web::application>);
  static_assert(std::is_copy_assignable_v<serveza::web::application>);
  EXPECT_TRUE(serveza::web::application{application});
  EXPECT_THROW(std::move(routes).build(), std::logic_error);
}

TEST(ApplicationTest, DefaultApplicationIsEmpty)
{
  EXPECT_FALSE(serveza::web::application{});
}

TEST(RouterBuilderTest, ValidatesPatternsWhenBuilding)
{
  serveza::web::router routes;
  routes.get("/broken//path", noOpHandler());
  EXPECT_THROW(std::move(routes).build(), std::invalid_argument);
}

TEST(RouterBuilderTest, RejectsUnknownMethodRegistration)
{
  serveza::web::router routes;
  EXPECT_THROW(routes.route(boost::beast::http::verb::unknown, "/resource", noOpHandler()), std::invalid_argument);
  EXPECT_THROW(routes.use(boost::beast::http::verb::unknown, noOpHandler()), std::invalid_argument);
}

TEST(RouterBuilderTest, ValidatesApplicationSettings)
{
  const auto rejected = [](auto change) {
    serveza::web::settings value;
    change(value);
    EXPECT_THROW((void)serveza::web::router{value}, std::invalid_argument);
  };

  rejected([](auto& value) { value.request_timeout = std::chrono::seconds::zero(); });
  rejected([](auto& value) { value.keep_alive_timeout = std::chrono::seconds{-1}; });
  rejected([](auto& value) { value.tls_handshake_timeout = std::chrono::seconds::zero(); });
  rejected([](auto& value) { value.tls_shutdown_timeout = std::chrono::seconds::zero(); });
  rejected([](auto& value) { value.header_limit = 0; });
  rejected([](auto& value) {
    value.header_limit = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1U;
  });
  rejected([](auto& value) {
    value.header_limit = 1;
    value.body_limit = std::numeric_limits<std::size_t>::max();
  });
  rejected([](auto& value) { value.regex_segment_limit = 0; });
}

TEST(RouterMountTest, FlattensMultipleNestedLevelsAtBuildTime)
{
  serveza::web::router users;
  users.use(noOpHandler());
  users.get("/:id", noOpHandler());

  serveza::web::router version;
  version.mount("/users", std::move(users));

  serveza::web::router api;
  api.mount("/v1/", std::move(version));

  serveza::web::router root;
  root.use(noOpHandler());
  root.mount("/api", std::move(api));

  EXPECT_NO_THROW({
    auto application = std::move(root).build();
    EXPECT_TRUE(application);
  });
}

TEST(RouterMountTest, SupportsDynamicMountPrefixes)
{
  serveza::web::router tenant;
  tenant.get("/users/:id", noOpHandler());

  serveza::web::router root;
  root.mount("/:tenant", std::move(tenant));
  EXPECT_NO_THROW(std::move(root).build());
}

TEST(RouterMountTest, SupportsUseSyntaxForNestedRouters)
{
  serveza::web::router rooms;
  rooms.get("/:roomId/state/:eventType{/:stateKey}", noOpHandler());

  serveza::web::router client;
  client.use("/rooms", std::move(rooms));

  serveza::web::router matrix;
  matrix.use("/client/v3", std::move(client));

  serveza::web::router root;
  root.use("/_matrix", std::move(matrix));
  EXPECT_NO_THROW(std::move(root).build());
}

TEST(RouterBuilderTest, SupportsMethodScopedFallbackHandlers)
{
  serveza::web::router routes;
  routes.get(noOpHandler());
  routes.post(noOpHandler());
  routes.use(boost::beast::http::verb::put, noOpHandler());
  EXPECT_NO_THROW(std::move(routes).build());
}

TEST(RouterMountTest, RejectsEmptyPrefixAndConsumedChildren)
{
  serveza::web::router child;
  child.get("/route", noOpHandler());
  serveza::web::router root;
  EXPECT_THROW(root.mount("", std::move(child)), std::invalid_argument);

  serveza::web::router consumed;
  auto application = std::move(consumed).build();
  EXPECT_TRUE(application);
  serveza::web::router anotherRoot;
  EXPECT_THROW(anotherRoot.mount("/api", std::move(consumed)), std::logic_error);
}

TEST(RouterDispatchIndexTest, MergesLiteralAndVariableBranchesInRegistrationOrder)
{
  namespace details = serveza::web::details;
  namespace http = boost::beast::http;

  serveza::web::settings settings;
  std::vector<int> calls;
  std::vector<details::compiled_layer> layers;
  const auto addMiddleware = [&](int id, std::string pattern) {
    layers.push_back({http::verb::get,
                      details::compiled_pattern{std::move(pattern), details::compiled_pattern::mode::exact, settings},
                      details::make_layer(serveza::web::yield_layer{
                          [&calls, id](Context&, serveza::web::continuation& next, Yield yield) {
                            calls.push_back(id);
                            next(yield);
                          }})});
  };

  layers.push_back(
      {std::nullopt, std::nullopt,
       details::make_layer(serveza::web::yield_layer{[&calls](Context&, serveza::web::continuation& next, Yield yield) {
         calls.push_back(0);
         next(yield);
       }})});
  addMiddleware(1, "/:tenant/fixed");
  addMiddleware(99, "/:tenant/other");
  addMiddleware(2, "/api/:id");
  addMiddleware(98, "/api/other");
  layers.push_back({http::verb::get,
                    details::compiled_pattern{"/api/fixed", details::compiled_pattern::mode::exact, settings},
                    details::make_layer(serveza::web::yield_layer{[&calls](Context& ctx, Yield yield) {
                      calls.push_back(3);
                      ctx.async_send(http::status::ok, "done", yield);
                    }})});

  details::application_impl application{settings, std::move(layers)};
  RequestHarness request{http::verb::get, "/api/fixed"};
  request.run([&](Context&, Yield yield) { application.async_dispatch(request.state(), yield); });

  EXPECT_EQ(calls, (std::vector<int>{0, 1, 2, 3}));
  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::ok);
}

class InvalidDispatchPathTest : public testing::TestWithParam<std::string> {};

TEST_P(InvalidDispatchPathTest, FallsThroughToNotFound)
{
  namespace details = serveza::web::details;
  namespace http = boost::beast::http;

  serveza::web::settings settings;
  details::application_impl application{settings, {}};
  RequestHarness request{http::verb::get, GetParam()};
  request.run([&](Context&, Yield yield) { application.async_dispatch(request.state(), yield); });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::not_found);
}

INSTANTIATE_TEST_SUITE_P(InvalidIndexPaths, InvalidDispatchPathTest, testing::Values("*", "/first//second"));

TEST(RouterDispatchTest, RejectsPathsAboveTheDocumentedSegmentLimit)
{
  namespace details = serveza::web::details;
  namespace http = boost::beast::http;

  std::string path;
  for (std::size_t index = 0; index <= serveza::web::settings::maximum_path_segments; ++index)
    path += "/segment";

  details::application_impl application{serveza::web::settings{}, {}};
  RequestHarness request{http::verb::get, std::move(path)};
  request.run([&](Context&, Yield yield) { application.async_dispatch(request.state(), yield); });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::uri_too_long);
}

TEST(RouterDispatchTest, OptionsReportsAllowedMethods)
{
  namespace details = serveza::web::details;
  namespace http = boost::beast::http;

  serveza::web::settings settings;
  std::vector<details::compiled_layer> layers;
  layers.push_back({http::verb::get,
                    details::compiled_pattern{"/resource", details::compiled_pattern::mode::exact, settings},
                    details::make_layer(serveza::web::yield_layer{[](Context&, Yield) {}})});
  details::application_impl application{settings, std::move(layers)};
  RequestHarness request{http::verb::options, "/resource"};
  request.run([&](Context&, Yield yield) { application.async_dispatch(request.state(), yield); });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::no_content);
  EXPECT_EQ((*request.transport.response)[http::field::allow], "GET, HEAD, OPTIONS");
}

TEST(RouterDispatchTest, OptionsReportsEveryRegisteredBeastMethod)
{
  namespace details = serveza::web::details;
  namespace http = boost::beast::http;

  serveza::web::settings settings;
  std::vector<details::compiled_layer> layers;
  layers.push_back({http::verb::connect,
                    details::compiled_pattern{"/resource", details::compiled_pattern::mode::exact, settings},
                    details::make_layer(noOpHandler())});
  layers.push_back({http::verb::trace,
                    details::compiled_pattern{"/resource", details::compiled_pattern::mode::exact, settings},
                    details::make_layer(noOpHandler())});
  layers.push_back({http::verb::propfind,
                    details::compiled_pattern{"/resource", details::compiled_pattern::mode::exact, settings},
                    details::make_layer(noOpHandler())});
  details::application_impl application{settings, std::move(layers)};
  RequestHarness request{http::verb::options, "/resource"};
  request.run([&](Context&, Yield yield) { application.async_dispatch(request.state(), yield); });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ((*request.transport.response)[http::field::allow], "CONNECT, OPTIONS, TRACE, PROPFIND");
}

TEST(RouterDispatchTest, OptionsAsteriskReportsServerMethods)
{
  namespace details = serveza::web::details;
  namespace http = boost::beast::http;

  serveza::web::settings settings;
  std::vector<details::compiled_layer> layers;
  layers.push_back({http::verb::get,
                    details::compiled_pattern{"/one", details::compiled_pattern::mode::exact, settings},
                    details::make_layer(noOpHandler())});
  layers.push_back({http::verb::propfind,
                    details::compiled_pattern{"/two", details::compiled_pattern::mode::exact, settings},
                    details::make_layer(noOpHandler())});
  details::application_impl application{settings, std::move(layers)};
  RequestHarness request{http::verb::options, "*"};
  request.run([&](Context&, Yield yield) { application.async_dispatch(request.state(), yield); });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::no_content);
  EXPECT_EQ((*request.transport.response)[http::field::allow], "GET, HEAD, OPTIONS, PROPFIND");
}

TEST(RouterDispatchTest, HeadFallbackThatContinuesProducesNotFound)
{
  namespace details = serveza::web::details;
  namespace http = boost::beast::http;

  serveza::web::settings settings;
  std::vector<details::compiled_layer> layers;
  layers.push_back(
      {http::verb::get, details::compiled_pattern{"/resource", details::compiled_pattern::mode::exact, settings},
       details::make_layer(
           serveza::web::yield_layer{[](Context&, serveza::web::continuation& next, Yield yield) { next(yield); }})});
  details::application_impl application{settings, std::move(layers)};
  RequestHarness request{http::verb::head, "/resource"};
  request.run([&](Context&, Yield yield) { application.async_dispatch(request.state(), yield); });

  ASSERT_TRUE(request.transport.response);
  EXPECT_EQ(request.transport.response->result(), http::status::not_found);
  EXPECT_TRUE(request.transport.skippedBody);
  EXPECT_TRUE(request.state().response_route.empty());
}

TEST(RouterDispatchTest, RecordsTheRouteThatActuallyCommitsTheResponse)
{
  namespace details = serveza::web::details;
  namespace http = boost::beast::http;

  serveza::web::settings settings;
  std::string_view outerRouteAfterNext;
  std::vector<details::compiled_layer> layers;
  layers.push_back({http::verb::get,
                    details::compiled_pattern{"/:value", details::compiled_pattern::mode::exact, settings},
                    details::make_layer(serveza::web::yield_layer{
                        [&outerRouteAfterNext](Context& ctx, serveza::web::continuation& next, Yield yield) {
                          EXPECT_EQ(ctx.matched_route(), "/:value");
                          next(yield);
                          outerRouteAfterNext = ctx.matched_route();
                        }})});
  layers.push_back({http::verb::get,
                    details::compiled_pattern{"/resource", details::compiled_pattern::mode::exact, settings},
                    details::make_layer(serveza::web::yield_layer{[](Context& ctx, Yield yield) {
                      EXPECT_EQ(ctx.matched_route(), "/resource");
                      ctx.async_send(http::status::ok, "done", yield);
                    }})});

  details::application_impl application{settings, std::move(layers)};
  RequestHarness request{http::verb::get, "/resource"};
  request.run([&](Context&, Yield yield) { application.async_dispatch(request.state(), yield); });

  EXPECT_EQ(outerRouteAfterNext, "/:value");
  EXPECT_EQ(request.state().response_route, "/resource");
  EXPECT_TRUE(request.state().current_route.empty());
}

TEST(RouterDispatchTest, CannotContinueAfterWebSocketUpgrade)
{
  namespace details = serveza::web::details;
  namespace http = boost::beast::http;

  serveza::web::settings settings;
  std::vector<details::compiled_layer> layers;
  layers.push_back({http::verb::get, std::nullopt,
                    details::make_layer(serveza::web::yield_layer{
                        [](Context&, serveza::web::continuation& next, Yield yield) { next(yield); }})});
  details::application_impl application{settings, std::move(layers)};
  RequestHarness request;
  request.state().websocket_upgraded = true;

  EXPECT_THROW(request.run([&](Context&, Yield yield) { application.async_dispatch(request.state(), yield); }),
               std::logic_error);
}

TEST(RouterDispatchTest, PreservesContinuationExecutorAndCancellationSlot)
{
  namespace details = serveza::web::details;
  namespace http = boost::beast::http;

  serveza::web::settings settings;
  boost::asio::io_context completionIo;
  boost::asio::cancellation_signal signal;
  bool observedExecutor{};
  bool observedCancellation{};
  bool completed{};
  std::vector<details::compiled_layer> layers;
  layers.push_back(
      {http::verb::get, std::nullopt,
       details::make_layer([&](Context&, serveza::web::continuation& next, serveza::web::completion_handler done) {
         next(boost::asio::bind_cancellation_slot(
             signal.slot(),
             boost::asio::bind_executor(completionIo.get_executor(),
                                        [done = std::move(done)](boost::system::error_code ec) mutable { done(ec); })));
       })});
  layers.push_back({http::verb::get,
                    details::compiled_pattern{"/resource", details::compiled_pattern::mode::exact, settings},
                    details::make_layer([&](Context& ctx, serveza::web::completion_handler done) {
                      observedExecutor =
                          boost::asio::get_associated_executor(done, ctx.get_executor()) == completionIo.get_executor();
                      observedCancellation = boost::asio::get_associated_cancellation_slot(done).is_connected();
                      done(boost::system::error_code{});
                    })});

  details::application_impl application{settings, std::move(layers)};
  RequestHarness request{http::verb::get, "/resource"};
  application.async_dispatch(request.state(), [&](boost::system::error_code ec) {
    EXPECT_FALSE(ec);
    completed = true;
  });

  EXPECT_FALSE(completed);
  request.io.run();
  EXPECT_TRUE(observedExecutor);
  EXPECT_TRUE(observedCancellation);
  EXPECT_FALSE(completed);
  completionIo.run();
  request.io.restart();
  request.io.run();
  EXPECT_TRUE(completed);
}

} // namespace
