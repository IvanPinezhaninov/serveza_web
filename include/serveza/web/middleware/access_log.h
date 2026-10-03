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

#ifndef SERVEZA_WEB_MIDDLEWARE_ACCESS_LOG_H
#define SERVEZA_WEB_MIDDLEWARE_ACCESS_LOG_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>

#include <serveza/web/export.h>
#include <serveza/web/middleware/traffic_observer.h>

namespace serveza::web {
class continuation;
class request_context;
} // namespace serveza::web

namespace serveza::web::middleware {

/** @brief Structured summary emitted after a request leaves the middleware chain. */
struct access_log_entry {
  /** @brief Request method. */
  boost::beast::http::verb method{boost::beast::http::verb::unknown};

  /** @brief Encoded request path without its query string. */
  std::string path;

  /** @brief Pattern of the terminal matched route, when available. */
  std::string route;

  /** @brief Committed response status, or no value if processing failed first. */
  std::optional<boost::beast::http::status> status;

  /** @brief Number of consumed request body bytes. */
  std::size_t request_body_size{};

  /** @brief Logical response body size, excluding headers and transfer framing. */
  std::size_t response_body_size{};

  /** @brief Whether the complete response was written successfully. */
  bool response_completed{};

  /** @brief Time from observer registration through final exchange completion. */
  std::chrono::microseconds elapsed{};

  /** @brief Request ID installed by @ref request_id, or an empty string. */
  std::string request_id;

  /** @brief Serveza listener identifier. */
  std::uint64_t listener_id{};

  /** @brief Serveza connection identifier, or zero when metadata is unavailable. */
  std::uint64_t connection_id{};

  /** @brief Printable remote endpoint, or an empty string when unavailable. */
  std::string remote_endpoint;

  /** @brief Failure escaping the chain, or an empty pointer after normal completion. */
  std::exception_ptr error;
};

namespace details {

using access_log_callback = void (*)(void*, const access_log_entry&);

SERVEZA_WEB_API void emit_access_log(const exchange_complete_event& event, void* sink,
                                     access_log_callback callback) noexcept;

} // namespace details

/**
 * @brief Adapts traffic-observer completion events to owning log records.
 *
 * The sink is stored directly without @c std::function. Sink failures are
 * ignored so logging cannot change the request result. The sink may be called
 * concurrently and must provide any required synchronization.
 */
template<typename Sink>
class access_log final {
public:
  /** @brief Stores @p sink by value. */
  explicit access_log(Sink sink)
    : m_sink{std::move(sink)}
  {
    static_assert(std::is_invocable_r_v<void, Sink&, const access_log_entry&>,
                  "access log sink must be callable with (const access_log_entry&)");
    if constexpr (std::is_pointer_v<Sink>) {
      if (!m_sink) throw std::invalid_argument{"access log sink must not be null"};
    }
  }

  /** @brief Observes exchange completion and emits its owning log record. */
  void operator()(request_context& ctx, continuation& next, completion_handler handler) const
  {
    details::traffic_observer_callbacks callbacks;
    callbacks.complete = &complete;
    details::invoke_observe_traffic(ctx, next, std::move(handler), std::addressof(m_sink), callbacks);
  }

private:
  static void complete(void* sink, const exchange_complete_event& event)
  {
    details::emit_access_log(event, sink, &write);
  }

  static void write(void* sink, const access_log_entry& entry)
  {
    (*static_cast<Sink*>(sink))(entry);
  }

  mutable Sink m_sink;
};

} // namespace serveza::web::middleware

#endif // SERVEZA_WEB_MIDDLEWARE_ACCESS_LOG_H
