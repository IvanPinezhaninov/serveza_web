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

#include "detail/route_pattern.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::atomic_size_t allocationCount{};
bool countAllocations{};
// Prevents the optimizer from removing completed matches.
volatile std::size_t resultSink{};

using Pattern = serveza::web::details::compiled_pattern;

struct RouteSet final {
  std::vector<Pattern> m_routes;
  std::string m_first;
  std::string m_last;
  std::string m_miss;
};

RouteSet makeRoutes(std::size_t count, std::string_view suffix)
{
  serveza::web::settings config;
  RouteSet result;
  result.m_routes.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    auto route = "/api/item" + std::to_string(i);
    result.m_routes.emplace_back(route + std::string{suffix}, Pattern::mode::exact, config);
  }
  const bool hasParameter = !suffix.empty();
  result.m_first = hasParameter ? "/api/item0/42" : "/api/item0";
  result.m_last =
      hasParameter ? "/api/item" + std::to_string(count - 1) + "/42" : "/api/item" + std::to_string(count - 1);
  result.m_miss = hasParameter ? "/api/missing/42" : "/api/missing";
  return result;
}

void runCase(const RouteSet& routes, std::string_view kind, std::string_view position, std::string_view target)
{
  const auto iterations = std::max<std::size_t>(1000, 200000 / routes.m_routes.size());
  serveza::web::route_params params;

  auto scan = [&] {
    bool matched = false;
    for (const auto& route : routes.m_routes) {
      if (!route.match(target, params)) continue;
      matched = true;
      break;
    }
    resultSink += matched ? 1U : 0U;
  };

  for (int i = 0; i < 32; ++i)
    scan();
  allocationCount.store(0, std::memory_order_relaxed);
  countAllocations = true;
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < iterations; ++i)
    scan();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  countAllocations = false;

  const auto nanoseconds = std::chrono::duration<double, std::nano>{elapsed}.count() / static_cast<double>(iterations);
  const auto allocations =
      static_cast<double>(allocationCount.load(std::memory_order_relaxed)) / static_cast<double>(iterations);
  std::cout << kind << ',' << routes.m_routes.size() << ',' << position << ',' << iterations << ',' << std::fixed
            << std::setprecision(2) << nanoseconds << ',' << allocations << '\n';
}

} // namespace

void* operator new(std::size_t size)
{
  if (countAllocations) allocationCount.fetch_add(1, std::memory_order_relaxed);
  if (void* value = std::malloc(size)) return value;
  throw std::bad_alloc{};
}

void operator delete(void* value) noexcept
{
  std::free(value);
}

void operator delete(void* value, std::size_t) noexcept
{
  std::free(value);
}

int main()
{
  std::cout << "kind,routes,position,iterations,ns_per_operation,allocations_per_operation\n";
  for (const auto count : {std::size_t{10}, std::size_t{100}, std::size_t{1000}}) {
    struct Variant {
      std::string_view m_kind;
      std::string_view m_suffix;
    };
    for (const auto value : {Variant{"exact", ""}, Variant{"dynamic", "/:id"}, Variant{"decimal", "/:id([0-9]+)"},
                             Variant{"regex", "/:id([0-9]{1,8})"}}) {
      const auto routes = makeRoutes(count, value.m_suffix);
      const auto kind = value.m_kind;
      runCase(routes, kind, "first", routes.m_first);
      runCase(routes, kind, "last", routes.m_last);
      runCase(routes, kind, "miss", routes.m_miss);
    }
  }
  return EXIT_SUCCESS;
}
