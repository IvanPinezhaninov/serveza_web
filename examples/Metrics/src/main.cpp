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
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

#include <serveza/serveza.h>
#include <serveza/web.h>
#include <serveza/web/yield.h>

namespace {

namespace http = boost::beast::http;
namespace net = boost::asio;
namespace web = serveza::web;

std::string escapeLabel(std::string_view value)
{
  std::string result;
  result.reserve(value.size());
  for (const char character : value) {
    if (character == '\\' || character == '"') result.push_back('\\');
    if (character == '\n') {
      result += "\\n";
    } else {
      result.push_back(character);
    }
  }
  return result;
}

void appendNumber(std::string& output, std::uint64_t value)
{
  char buffer[20];
  const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
  if (converted.ec != std::errc{}) throw std::runtime_error{"could not format metric value"};
  output.append(buffer, converted.ptr);
}

void appendNumber(std::string& output, double value)
{
  char buffer[64];
  const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general,
                                       std::numeric_limits<double>::max_digits10);
  if (converted.ec != std::errc{}) throw std::runtime_error{"could not format metric value"};
  output.append(buffer, converted.ptr);
}

class MetricsRegistry final {
  struct Key final {
    std::string m_method;
    std::string m_route;
    std::string m_status;
    std::string m_outcome;

    bool operator<(const Key& other) const noexcept
    {
      return std::tie(m_method, m_route, m_status, m_outcome) <
             std::tie(other.m_method, other.m_route, other.m_status, other.m_outcome);
    }
  };

  struct Totals final {
    std::uint64_t m_count{};
    std::uint64_t m_requestBytes{};
    std::uint64_t m_responseBytes{};
    double m_durationSeconds{};
  };

public:
  void begin() noexcept
  {
    m_inFlight.fetch_add(1, std::memory_order_relaxed);
  }

  void finish(const web::middleware::exchange_complete_event& event) noexcept
  {
    m_inFlight.fetch_sub(1, std::memory_order_relaxed);
    try {
      const auto methodView = http::to_string(event.method);
      const std::string method{methodView.data(), methodView.size()};
      const std::string route = event.route.empty() ? "<unmatched>" : std::string{event.route};
      const std::string status = event.status ? std::to_string(static_cast<unsigned>(*event.status)) : "none";
      const bool serverError = event.status && static_cast<unsigned>(*event.status) >= 500U;
      const std::string outcome = event.error || serverError || !event.response_completed ? "error" : "success";

      std::lock_guard lock{m_mutex};
      auto& value = m_completed[{method, route, status, outcome}];
      ++value.m_count;
      value.m_requestBytes += event.request_body_size;
      value.m_responseBytes += event.response_body_size;
      value.m_durationSeconds += std::chrono::duration<double>{event.elapsed}.count();
    } catch (...) {}
  }

  std::string render() const
  {
    std::lock_guard lock{m_mutex};
    std::string output;
    output.reserve(256 + m_completed.size() * 512);
    output += "# TYPE serveza_http_requests_in_flight gauge\n";
    output += "serveza_http_requests_in_flight ";
    appendNumber(output, m_inFlight.load(std::memory_order_relaxed));
    output += "\n# TYPE serveza_http_requests_total counter\n";
    for (const auto& [metricKey, value] : m_completed) {
      output += "serveza_http_requests_total";
      output += labels(metricKey);
      output.push_back(' ');
      appendNumber(output, value.m_count);
      output.push_back('\n');
    }

    output += "# TYPE serveza_http_request_body_bytes_total counter\n";
    for (const auto& [metricKey, value] : m_completed) {
      output += "serveza_http_request_body_bytes_total";
      output += labels(metricKey);
      output.push_back(' ');
      appendNumber(output, value.m_requestBytes);
      output.push_back('\n');
    }

    output += "# TYPE serveza_http_response_body_bytes_total counter\n";
    for (const auto& [metricKey, value] : m_completed) {
      output += "serveza_http_response_body_bytes_total";
      output += labels(metricKey);
      output.push_back(' ');
      appendNumber(output, value.m_responseBytes);
      output.push_back('\n');
    }

    output += "# TYPE serveza_http_request_duration_seconds summary\n";
    for (const auto& [metricKey, value] : m_completed) {
      output += "serveza_http_request_duration_seconds_sum";
      output += labels(metricKey);
      output.push_back(' ');
      appendNumber(output, value.m_durationSeconds);
      output += "\nserveza_http_request_duration_seconds_count";
      output += labels(metricKey);
      output.push_back(' ');
      appendNumber(output, value.m_count);
      output.push_back('\n');
    }
    return output;
  }

private:
  static std::string labels(const Key& value)
  {
    return "{method=\"" + escapeLabel(value.m_method) + "\",route=\"" + escapeLabel(value.m_route) + "\",status=\"" +
           escapeLabel(value.m_status) + "\",outcome=\"" + escapeLabel(value.m_outcome) + "\"}";
  }

  std::atomic_uint64_t m_inFlight{};
  mutable std::mutex m_mutex;
  std::map<Key, Totals> m_completed;
};

class PrometheusObserver final {
public:
  explicit PrometheusObserver(std::shared_ptr<MetricsRegistry> registry)
    : m_registry{std::move(registry)}
  {}

  void operator()(const web::middleware::request_headers_event&) const noexcept
  {
    m_registry->begin();
  }

  void operator()(const web::middleware::exchange_complete_event& event) const noexcept
  {
    m_registry->finish(event);
  }

private:
  std::shared_ptr<MetricsRegistry> m_registry;
};

class HelloEndpoint final {
public:
  void operator()(web::request_context& ctx, net::yield_context yield) const
  {
    ctx.async_send(http::status::ok, "hello\n", yield);
  }
};

class EchoEndpoint final {
public:
  void operator()(web::request_context& ctx, net::yield_context yield) const
  {
    const auto contentType = ctx.request_headers()[http::field::content_type];
    ctx.async_send(http::status::ok, std::string{ctx.async_read_body(yield)},
                   std::string_view{contentType.data(), contentType.size()}, yield);
  }
};

class MetricsEndpoint final {
public:
  explicit MetricsEndpoint(std::shared_ptr<MetricsRegistry> metrics)
    : m_metrics{std::move(metrics)}
  {}

  void operator()(web::request_context& ctx, net::yield_context yield) const
  {
    ctx.async_send(http::status::ok, m_metrics->render(), "text/plain; version=0.0.4; charset=utf-8", yield);
  }

private:
  std::shared_ptr<MetricsRegistry> m_metrics;
};

} // namespace

int main()
{
  try {
    auto metrics = std::make_shared<MetricsRegistry>();
    web::router routes;
    routes.use(web::middleware::observe_traffic{PrometheusObserver{metrics}});
    routes.get("/", web::yield_layer{HelloEndpoint{}});
    routes.post("/echo", web::yield_layer{EchoEndpoint{}});
    routes.get("/metrics", web::yield_layer{MetricsEndpoint{metrics}});

    const auto app = std::move(routes).build();
    net::io_context io;
    serveza::server server{io.get_executor()};
    auto listener =
        server.listen<net::ip::tcp>({net::ip::address_v4::loopback(), 8080}, [app] { return web::http_session{app}; });

    net::signal_set signals{io, SIGINT, SIGTERM};
    signals.async_wait([&server](boost::system::error_code ec, int) {
      if (!ec) server.request_stop();
    });

    int result = EXIT_SUCCESS;
    server.async_wait([&](boost::system::error_code ec) {
      if (ec) {
        std::cerr << "Server stopped with error: " << ec.message() << std::endl;
        result = EXIT_FAILURE;
      }
      signals.cancel();
    });
    server.async_start([&, listener](boost::system::error_code ec) {
      if (ec) {
        std::cerr << "Server start failed: " << ec.message() << std::endl;
        result = EXIT_FAILURE;
        server.request_stop();
        return;
      }
      std::cout << "Listening on http://" << listener->endpoint() << '\n'
                << "  GET  /\n"
                << "  POST /echo\n"
                << "  GET  /metrics" << std::endl;
    });

    io.run();
    return result;
  } catch (const std::exception& e) {
    std::cerr << "Example failed: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}
