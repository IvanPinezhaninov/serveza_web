# Serveza Web 🍺

**The web server you’d raise a glass to.**

[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Build](https://img.shields.io/github/actions/workflow/status/IvanPinezhaninov/serveza_web/build_and_test.yml?label=Build)](https://github.com/IvanPinezhaninov/serveza_web/actions/workflows/build_and_test.yml)
[![Coverage](https://img.shields.io/endpoint?url=https%3A%2F%2Fivanpinezhaninov.github.io%2Fserveza_web%2Fcoverage.json&label=Coverage)](https://ivanpinezhaninov.github.io/serveza_web/)

Serveza Web is a small C++17 web library built on Serveza, Boost.Asio and
Boost.Beast. It handles routing, middleware, request bodies, static files,
cookies, HTTPS and WebSockets.

Write endpoints and middleware with callbacks, `boost::asio::yield_context`, or
C++20 coroutines. The library itself stays on C++17.

## Yield HTTP server

This example adds a response header in middleware, then returns a short message
from a route. Both callables use `yield_context`.

```cpp
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <utility>

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

#include <serveza/serveza.h>
#include <serveza/web.h>
#include <serveza/web/yield.h>

namespace http = boost::beast::http;
namespace net = boost::asio;
namespace sys = boost::system;
namespace web = serveza::web;
using tcp = net::ip::tcp;

namespace {

constexpr unsigned short port = 8080;

class ServerHeaderMiddleware final {
public:
  void operator()(web::request_context& ctx, web::continuation& next, net::yield_context yield) const
  {
    ctx.response_headers().set(http::field::server, "Serveza Web");
    next(yield);
  }
};

class HelloEndpoint final {
public:
  void operator()(web::request_context& ctx, net::yield_context yield) const
  {
    ctx.async_send(http::status::ok, "Hello from Serveza Web!", yield);
  }
};

} // namespace

int main()
{
  web::router routes;
  routes.use(web::yield_layer{ServerHeaderMiddleware{}});
  routes.get("/", web::yield_layer{HelloEndpoint{}});

  const auto app = std::move(routes).build();
  net::io_context io;
  serveza::server server{io.get_executor()};
  const tcp::endpoint endpoint{net::ip::address_v4::loopback(), port};
  auto listener = server.listen<tcp>(endpoint, [app] {
    return web::http_session{app};
  });

  net::signal_set signals{io, SIGINT, SIGTERM};
  signals.async_wait([&server](sys::error_code ec, int) {
    if (!ec) server.request_stop();
  });

  int result = EXIT_SUCCESS;
  server.async_wait([&](sys::error_code ec) {
    if (ec) {
      std::cerr << "Server stopped with error: " << ec.message() << std::endl;
      result = EXIT_FAILURE;
    }
    signals.cancel();
  });
  server.async_start([&, listener](sys::error_code ec) {
    if (ec) {
      std::cerr << "Server start failed: " << ec.message() << std::endl;
      result = EXIT_FAILURE;
      server.request_stop();
      return;
    }
    std::cout << "Listening on http://" << listener->endpoint() << '/' << std::endl;
  });

  io.run();
  return result;
}
```

The core umbrella `<serveza/web.h>` exposes callback layers and token-generic
`async_*` operations without including Asio's stackful or C++20 coroutine
machinery. Stackful routes explicitly include `<serveza/web/yield.h>` and use
`yield_layer`, `yield_chunk_producer`, or `yield_websocket_endpoint` at the
callback boundary. C++20 routes similarly include `<serveza/web/awaitable.h>`
and use the corresponding `awaitable_*` adapter.

Programs using `yield_context` also link with `Boost::coroutine`. Callback and
C++20 coroutine programs only need `serveza::web`. The library itself does not
link with Boost.Coroutine or Boost.Context.

Each asynchronous style has its own small program. See [Callback](examples/Callback/src/main.cpp),
[Yield](examples/Yield/src/main.cpp) and [Coroutine](examples/Coroutine/src/main.cpp).

[Routing](examples/Routing/src/main.cpp) and [Middleware](examples/Middleware/src/main.cpp)
show how an application fits together. The other examples cover
[request bodies](examples/Bodies/src/main.cpp), [cookies](examples/Cookies/src/main.cpp),
[static files](examples/StaticFiles/src/main.cpp),
[traffic observation](examples/TrafficObserver/src/main.cpp) and
[WebSockets](examples/WebSocket/src/main.cpp).

## Build

From a source checkout:

```sh
cmake -S . -B build \
  -DSERVEZA_WEB_BUILD_EXAMPLES=ON \
  -DSERVEZA_WEB_BUILD_TESTS=ON \
  -DSERVEZA_WEB_BUILD_INTEGRATION_TESTS=ON \
  -DSERVEZA_WEB_BUILD_CPP20=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Serveza Web requires CMake 3.28 and Boost 1.83 or newer. If a parent project
already has dependency targets, Serveza Web uses them. Otherwise it downloads
the pinned Boost, OpenSSL when TLS is enabled, and GoogleTest when tests are
enabled. Set the corresponding `SERVEZA_WEB_USE_BUNDLED_*` option to `OFF` to
use an installed package.

C++20 coroutine tests and examples are optional and disabled by default. Set
`SERVEZA_WEB_BUILD_CPP20=ON` when the compiler and Boost.Asio support C++20
coroutines. Callback and `yield_context` targets remain available in a clean
C++17 build.

Serveza is connected through `FetchContent` after these dependencies are ready,
so it reuses the same targets. By default CMake downloads the pinned revision
from [IvanPinezhaninov/serveza](https://github.com/IvanPinezhaninov/serveza).
A parent project can also provide `serveza::serveza` before adding Serveza Web.

When Serveza Web is installed or added to a parent build, link the same target:

```cmake
find_package(serveza_web 0.1 CONFIG REQUIRED)
target_link_libraries(MyApplication PRIVATE serveza::web)
```

## License

Serveza Web is distributed under the [MIT License](LICENSE).

## Author

[Ivan Pinezhaninov](mailto:ivan.pinezhaninov@gmail.com)
