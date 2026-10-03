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

#ifndef SERVEZA_WEB_MIDDLEWARE_TRAFFIC_OBSERVER_H
#define SERVEZA_WEB_MIDDLEWARE_TRAFFIC_OBSERVER_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>

#include <serveza/web/async.h>
#include <serveza/web/body.h>
#include <serveza/web/export.h>

namespace serveza::web {
class continuation;
class request_context;
} // namespace serveza::web

namespace serveza::web::middleware {

/** @brief Describes how a response body is transferred. */
enum class response_body_kind {
  /** @brief The response has no body bytes. */
  empty,

  /** @brief The response body is held in memory. */
  buffered,

  /** @brief The response body is produced as HTTP chunks. */
  chunked,

  /** @brief The response body is streamed directly from a file. */
  file,

  /** @brief The HTTP connection is upgraded to WebSocket. */
  websocket,
};

/** @brief Non-owning request-header observation emitted before the remaining chain runs. */
struct request_headers_event final {
  /** @brief Context exposing the method, target, headers and connection metadata. */
  const request_context& context;

  /** @brief Opaque identity shared by every event for this request. */
  const void* exchange;
};

/** @brief Non-owning chunk of a consumed request body. */
struct request_body_chunk_event final {
  /** @brief Context of the observed request. */
  const request_context& context;

  /** @brief Opaque identity shared by every event for this request. */
  const void* exchange;

  /** @brief Bytes valid only for the duration of the sink call. */
  byte_view chunk;
};

/** @brief Non-owning response-header observation emitted around response commit. */
struct response_headers_event final {
  /** @brief Context of the observed request. */
  const request_context& context;

  /** @brief Opaque identity shared by every event for this request. */
  const void* exchange;

  /** @brief HTTP response status. */
  boost::beast::http::status status;

  /** @brief Final Beast header fields, including automatically generated fields. */
  const boost::beast::http::fields& headers;

  /** @brief Response body transfer kind. */
  response_body_kind body_kind;

  /** @brief Logical body size when known before transmission. */
  std::optional<std::uint64_t> body_size;
};

/** @brief Non-owning chunk of a successfully written buffered or chunked response body. */
struct response_body_chunk_event final {
  /** @brief Context of the observed request. */
  const request_context& context;

  /** @brief Opaque identity shared by every event for this request. */
  const void* exchange;

  /** @brief Bytes valid only for the duration of the sink call. */
  byte_view chunk;
};

/** @brief Metadata for a successfully written file response. */
struct response_file_event final {
  /** @brief Context of the observed request. */
  const request_context& context;

  /** @brief Opaque identity shared by every event for this request. */
  const void* exchange;

  /** @brief File path valid only for the duration of the sink call. */
  const std::filesystem::path& path;

  /** @brief Offset of the first represented file byte. */
  std::uint64_t offset;

  /** @brief Number of file bytes represented by the response. */
  std::uint64_t size;
};

/** @brief Notification that request dispatch and any generated error response have finished. */
struct exchange_complete_event final {
  /** @brief Context of the completed request. */
  const request_context& context;

  /** @brief Opaque identity shared by every event for this request. */
  const void* exchange;

  /** @brief Request method suitable for a bounded metrics label. */
  boost::beast::http::verb method{boost::beast::http::verb::unknown};

  /** @brief Matched route pattern valid during the callback, or an empty view when unmatched. */
  std::string_view route;

  /** @brief Final response status, or no value if no response could be committed. */
  std::optional<boost::beast::http::status> status;

  /** @brief Number of request body bytes consumed, including bytes discarded for keep-alive. */
  std::size_t request_body_size{};

  /** @brief Logical response body size, excluding headers and transfer framing. */
  std::size_t response_body_size{};

  /** @brief Whether the complete response, including its body terminator, was written successfully. */
  bool response_completed{};

  /** @brief Time elapsed since this observer was registered for the request. */
  std::chrono::microseconds elapsed{};

  /** @brief Failure escaping dispatch, or an empty pointer after normal completion. */
  std::exception_ptr error;
};

/// @cond INTERNAL
namespace details {

using request_headers_callback = void (*)(void*, const request_headers_event&);
using request_body_callback = void (*)(void*, const request_body_chunk_event&);
using response_headers_callback = void (*)(void*, const response_headers_event&);
using response_body_callback = void (*)(void*, const response_body_chunk_event&);
using response_file_callback = void (*)(void*, const response_file_event&);
using exchange_complete_callback = void (*)(void*, const exchange_complete_event&);

struct traffic_observer_callbacks final {
  request_headers_callback request_headers{};
  request_body_callback request_body{};
  response_headers_callback response_headers{};
  response_body_callback response_body{};
  response_file_callback response_file{};
  exchange_complete_callback complete{};
};

SERVEZA_WEB_API void invoke_observe_traffic(request_context& ctx, continuation& next, completion_handler handler,
                                            void* sink, traffic_observer_callbacks callbacks);

} // namespace details
/// @endcond

/**
 * @brief Exposes HTTP headers and body chunks to a caller-owned typed sink.
 *
 * The sink may overload @c operator() for any subset of the event types in
 * this header. Events are delivered synchronously, contain non-owning views,
 * and are ignored if the sink throws. The sink decides redaction, sampling,
 * truncation, persistence and log formatting. It may be called concurrently
 * and must provide any required synchronization.
 *
 * Request bodies are observed only as they are consumed. Buffered and chunked
 * response bodies are exposed as byte chunks without copying. File responses
 * expose their path and size instead of rereading file contents. Completion
 * events contain a method/route/status/bytes/duration summary suitable for
 * bounded-cardinality metrics without correlating earlier chunks.
 */
template<typename Sink>
class observe_traffic final {
public:
  /** @brief Stores @p sink by value. */
  explicit observe_traffic(Sink sink)
    : m_sink{std::move(sink)}
  {
    static_assert(observes_event<request_headers_event> || observes_event<request_body_chunk_event> ||
                      observes_event<response_headers_event> || observes_event<response_body_chunk_event> ||
                      observes_event<response_file_event> || observes_event<exchange_complete_event>,
                  "traffic observer sink must accept at least one traffic event type");
    if constexpr (std::is_pointer_v<Sink>) {
      if (!m_sink) throw std::invalid_argument{"traffic observer sink must not be null"};
    }
  }

  /** @brief Registers the sink for this request and runs the remaining chain. */
  void operator()(request_context& ctx, continuation& next, completion_handler handler) const
  {
    details::invoke_observe_traffic(ctx, next, std::move(handler), std::addressof(m_sink), callbacks());
  }

private:
  template<typename Event>
  static constexpr bool observes_event = std::is_invocable_r_v<void, Sink&, const Event&>;

  template<typename Event>
  static void emit(void* sink, const Event& event)
  {
    if constexpr (observes_event<Event>) (*static_cast<Sink*>(sink))(event);
  }

  static constexpr details::traffic_observer_callbacks callbacks() noexcept
  {
    return {
        observes_event<request_headers_event> ? &emit<request_headers_event> : nullptr,
        observes_event<request_body_chunk_event> ? &emit<request_body_chunk_event> : nullptr,
        observes_event<response_headers_event> ? &emit<response_headers_event> : nullptr,
        observes_event<response_body_chunk_event> ? &emit<response_body_chunk_event> : nullptr,
        observes_event<response_file_event> ? &emit<response_file_event> : nullptr,
        observes_event<exchange_complete_event> ? &emit<exchange_complete_event> : nullptr,
    };
  }

  mutable Sink m_sink;
};

} // namespace serveza::web::middleware

#endif // SERVEZA_WEB_MIDDLEWARE_TRAFFIC_OBSERVER_H
