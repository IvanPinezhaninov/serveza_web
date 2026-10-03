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

#include "AllocationBackend.h"
#include "detail/core.h"
#include "detail/route_pattern.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/beast/http.hpp>

#include <serveza/web/context.h>
#include <serveza/web/detail/layer.h>
#include <serveza/web/middleware.h>
#include <serveza/web/settings.h>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
namespace web = serveza::web;

std::atomic_size_t allocationCount{};
bool countAllocations{};
// Prevents the optimizer from removing completed responses.
volatile std::size_t resultSink{};

class NullTransport final : public web::details::transport {
public:
  explicit NullTransport(net::any_io_executor executor)
    : m_executor{std::move(executor)}
    , m_completionExecutor{m_executor}
  {}

  net::any_io_executor get_executor() override
  {
    return m_executor;
  }

  const net::any_completion_executor& get_completion_executor() const noexcept override
  {
    return m_completionExecutor;
  }

  void async_read_header(boost::beast::flat_buffer&, web::details::parser_type&,
                         web::details::completion_callback) override
  {
    throw std::logic_error{"unexpected async_read_header"};
  }

  void async_read_some(boost::beast::flat_buffer&, web::details::parser_type&,
                       web::details::completion_callback) override
  {
    throw std::logic_error{"unexpected async_read_some"};
  }

  void async_write(web::details::string_response& response, bool, web::details::completion_callback handler) override
  {
    resultSink += response.body().size();
    handler(boost::system::error_code{});
  }

  void async_write(web::details::byte_response& response, bool, web::details::completion_callback handler) override
  {
    resultSink += response.body().size();
    handler(boost::system::error_code{});
  }

  void async_write(web::details::file_response&, bool, web::details::completion_callback) override
  {
    throw std::logic_error{"unexpected file response"};
  }

  void async_write_file_range(web::details::empty_response&, boost::beast::file&, std::uint64_t, bool,
                              web::details::completion_callback) override
  {
    throw std::logic_error{"unexpected file range response"};
  }

  void async_write_chunked_header(web::details::empty_response&, bool, web::details::completion_callback) override
  {
    throw std::logic_error{"unexpected chunked response"};
  }

  void async_write_chunk(const void*, std::size_t, web::details::completion_callback) override
  {
    throw std::logic_error{"unexpected chunk"};
  }

  void async_write_chunk_last(web::details::completion_callback) override
  {
    throw std::logic_error{"unexpected final chunk"};
  }

  void async_accept_websocket(const web::details::parser_type&, const http::fields&, const web::websocket_options&,
                              void*, websocket_callback, web::details::completion_callback) override
  {
    throw std::logic_error{"unexpected WebSocket upgrade"};
  }

private:
  net::any_io_executor m_executor;
  net::any_completion_executor m_completionExecutor;
};

struct NoOpContinuation final {
  static void run(void*, web::completion_handler handler)
  {
    handler(boost::system::error_code{});
  }
};

struct BenchmarkCase final {
  std::string m_name;
  std::string m_target;
  std::shared_ptr<web::details::layer> m_callback;
  std::unique_ptr<web::details::application_impl> m_application;
  bool m_direct{};
};

std::shared_ptr<web::details::layer> makeCallback()
{
  return web::details::make_layer([](web::request_context& ctx, web::completion_handler handler) {
    ctx.async_send(http::status::ok, "ok", std::move(handler));
  });
}

std::unique_ptr<web::details::application_impl> makeApplication(const std::vector<std::string>& patterns,
                                                                const std::shared_ptr<web::details::layer>& callback)
{
  web::settings config;
  std::vector<web::details::compiled_layer> layers;
  layers.reserve(patterns.size());
  for (const auto& source : patterns) {
    layers.push_back({http::verb::get,
                      web::details::compiled_pattern{source, web::details::compiled_pattern::mode::exact, config},
                      callback});
  }
  return std::make_unique<web::details::application_impl>(std::move(config), std::move(layers));
}

BenchmarkCase makeSingle(std::string name, std::string pattern, std::string target)
{
  auto callback = makeCallback();
  auto application = makeApplication({pattern}, callback);
  return {std::move(name), std::move(target), std::move(callback), std::move(application), false};
}

BenchmarkCase makeMany(std::string name, std::size_t count, bool indexed, bool hitFirst)
{
  std::vector<std::string> patterns;
  patterns.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    if (indexed)
      patterns.push_back("/route" + std::to_string(i) + "/value");
    else
      patterns.push_back("/api/item" + std::to_string(i));
  }

  auto callback = makeCallback();
  auto application = makeApplication(patterns, callback);
  const auto index = hitFirst ? std::size_t{} : count - 1;
  const auto target = indexed ? "/route" + std::to_string(index) + "/value" : "/api/item" + std::to_string(index);
  return {std::move(name), std::move(target), std::move(callback), std::move(application), false};
}

BenchmarkCase makeFallback(std::string name, std::size_t count, bool hitFirst)
{
  std::vector<std::string> patterns;
  patterns.reserve(count);
  for (std::size_t i = 0; i < count; ++i)
    patterns.push_back("/:tenant/item" + std::to_string(i));

  auto callback = makeCallback();
  auto application = makeApplication(patterns, callback);
  const auto index = hitFirst ? std::size_t{} : count - 1;
  return {std::move(name), "/tenant/item" + std::to_string(index), std::move(callback), std::move(application), false};
}

void invokeDirect(web::details::layer& callback, web::details::request_state& state, web::completion_handler completion)
{
  auto ctx = state.make_context();
  NoOpContinuation continuationState;
  auto next = web::details::continuation_access::make(&continuationState, &NoOpContinuation::run);
  callback.async_invoke(ctx, next, std::move(completion));
}

struct Sample final {
  double m_nanoseconds{};
  double m_allocations{};
};

Sample measure(const BenchmarkCase& value, std::size_t iterations)
{
  net::io_context io;
  NullTransport transport{io.get_executor()};
  boost::beast::flat_buffer buffer;
  web::details::parser_type parser;
  parser.get().method(http::verb::get);
  parser.get().target(value.m_target);
  parser.get().version(11);
  parser.get().keep_alive(true);
  const auto target = web::details::parse_target(value.m_target);
  const auto& config = value.m_application->config();

  Sample result;
  const auto invoke = [&] {
    web::details::request_state state{transport, buffer, parser, target, config, 0, nullptr};
    state.force_close = true;
    boost::system::error_code ec;
    bool completed{};
    auto completion = [&](boost::system::error_code operationEc) {
      ec = operationEc;
      completed = true;
    };
    if (value.m_direct)
      invokeDirect(*value.m_callback, state, std::move(completion));
    else
      value.m_application->async_dispatch(state, std::move(completion));
    if (!completed) {
      io.restart();
      io.run();
    }
    if (ec) throw boost::system::system_error{ec};
    resultSink += state.response != web::details::response_progress::idle ? 1U : 0U;
  };

  for (std::size_t i = 0; i < 4096; ++i)
    invoke();

  allocationCount.store(0, std::memory_order_relaxed);
  countAllocations = true;
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < iterations; ++i)
    invoke();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  countAllocations = false;

  result.m_nanoseconds = std::chrono::duration<double, std::nano>{elapsed}.count() / static_cast<double>(iterations);
  result.m_allocations =
      static_cast<double>(allocationCount.load(std::memory_order_relaxed)) / static_cast<double>(iterations);
  return result;
}

void runCase(const BenchmarkCase& value)
{
  constexpr std::size_t samplesCount = 7;
  const auto iterations = value.m_name.find("1000") == std::string::npos ? std::size_t{500000} : std::size_t{20000};
  std::array<Sample, samplesCount> samples;
  for (auto& current : samples)
    current = measure(value, iterations);

  std::sort(samples.begin(), samples.end(),
            [](const auto& left, const auto& right) { return left.m_nanoseconds < right.m_nanoseconds; });
  const auto& median = samples[samplesCount / 2];
  std::cout << value.m_name << ',' << iterations << ',' << std::fixed << std::setprecision(2) << median.m_nanoseconds
            << ',' << samples.front().m_nanoseconds << ',' << samples.back().m_nanoseconds << ','
            << median.m_allocations << '\n';
}

} // namespace

void* operator new(std::size_t size)
{
  if (countAllocations) allocationCount.fetch_add(1, std::memory_order_relaxed);
  if (void* value = servezaWebBenchmarks::details::allocate(size)) return value;
  throw std::bad_alloc{};
}

void operator delete(void* value) noexcept
{
  servezaWebBenchmarks::details::deallocate(value);
}

void operator delete(void* value, std::size_t) noexcept
{
  servezaWebBenchmarks::details::deallocate(value);
}

int main()
{
  std::vector<BenchmarkCase> cases;
  auto baseline = makeSingle("direct_handler", "/unused", "/exact");
  baseline.m_direct = true;
  cases.push_back(std::move(baseline));
  cases.push_back(makeSingle("exact_1", "/exact", "/exact"));
  cases.push_back(makeSingle("dynamic_1", "/users/:id", "/users/42"));
  cases.push_back(makeSingle("decimal_1", "/users/:id([0-9]+)", "/users/42"));
  cases.push_back(makeSingle("regex_1", "/users/:id([0-9]{1,8})", "/users/42"));
  cases.push_back(makeMany("indexed_1000_last", 1000, true, false));
  cases.push_back(makeMany("bucket_1000_first", 1000, false, true));
  cases.push_back(makeMany("bucket_1000_last", 1000, false, false));
  cases.push_back(makeFallback("fallback_1000_first", 1000, true));
  cases.push_back(makeFallback("fallback_1000_last", 1000, false));

  std::cout << "case,iterations,median_ns,min_ns,max_ns,allocations_per_operation\n";
  for (const auto& value : cases)
    runCase(value);
  return EXIT_SUCCESS;
}
