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

#include <serveza/web/middleware/traffic_observer.h>

#include <serveza/web/context.h>
#include <serveza/web/middleware.h>

#include "detail/core.h"

namespace serveza::web::details {

namespace {

template<typename Callback, typename Event>
void notify(const request_state::traffic_observer_binding& observer, Callback callback, const Event& event) noexcept
{
  if (!callback) return;
  try {
    callback(observer.sink, event);
  } catch (...) {}
}

} // namespace

void observe_request_body(request_state& state, byte_view chunk) noexcept
{
  state.request_body_size += chunk.size();
  if (chunk.empty() || state.traffic_observers.empty()) return;
  auto ctx = state.make_context();
  const middleware::request_body_chunk_event event{ctx, &state, chunk};
  for (const auto& observer : state.traffic_observers)
    notify(observer, observer.callbacks.request_body, event);
}

void observe_response_headers(request_state& state, boost::beast::http::status status,
                              const boost::beast::http::fields& headers, middleware::response_body_kind body_kind,
                              std::optional<std::uint64_t> body_size) noexcept
{
  if (state.traffic_observers.empty()) return;
  auto ctx = state.make_context();
  const middleware::response_headers_event event{ctx, &state, status, headers, body_kind, body_size};
  for (const auto& observer : state.traffic_observers)
    notify(observer, observer.callbacks.response_headers, event);
}

void observe_response_body(request_state& state, byte_view chunk) noexcept
{
  if (chunk.empty() || state.traffic_observers.empty()) return;
  auto ctx = state.make_context();
  const middleware::response_body_chunk_event event{ctx, &state, chunk};
  for (const auto& observer : state.traffic_observers)
    notify(observer, observer.callbacks.response_body, event);
}

void observe_response_file(request_state& state, const std::filesystem::path& path, std::uint64_t offset,
                           std::uint64_t size) noexcept
{
  if (state.traffic_observers.empty()) return;
  auto ctx = state.make_context();
  const middleware::response_file_event event{ctx, &state, path, offset, size};
  for (const auto& observer : state.traffic_observers)
    notify(observer, observer.callbacks.response_file, event);
}

void complete_traffic_observers(request_state& state, std::exception_ptr ep) noexcept
{
  if (state.traffic_observers.empty()) return;
  auto ctx = state.make_context();
  const auto completed = std::chrono::steady_clock::now();
  for (const auto& observer : state.traffic_observers) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(completed - observer.started);
    const middleware::exchange_complete_event event{ctx,
                                                    &state,
                                                    ctx.method(),
                                                    state.response_route,
                                                    ctx.response_status(),
                                                    ctx.request_body_size(),
                                                    ctx.response_body_size(),
                                                    state.response == response_progress::completed,
                                                    elapsed,
                                                    ep};
    notify(observer, observer.callbacks.complete, event);
  }
}

} // namespace serveza::web::details

namespace serveza::web::middleware::details {

void invoke_observe_traffic(request_context& ctx, continuation& next, completion_handler handler, void* sink,
                            traffic_observer_callbacks callbacks)
{
  auto& state = web::details::traffic_observer_access::state(ctx);
  state.traffic_observers.push_back({sink, callbacks, std::chrono::steady_clock::now()});
  const auto& observer = state.traffic_observers.back();
  if (observer.callbacks.request_headers) {
    try {
      observer.callbacks.request_headers(observer.sink, request_headers_event{ctx, &state});
    } catch (...) {}
  }
  next(std::move(handler));
}

} // namespace serveza::web::middleware::details
