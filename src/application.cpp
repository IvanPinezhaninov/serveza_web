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

#include <serveza/web/application.h>

#include "detail/core.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/system/errc.hpp>

namespace serveza::web {

application::application() noexcept = default;

application::operator bool() const noexcept
{
  return static_cast<bool>(m_impl);
}

application::application(std::shared_ptr<const details::application_impl> impl) noexcept
  : m_impl{std::move(impl)}
{}

} // namespace serveza::web

namespace serveza::web::details {

namespace http = boost::beast::http;

namespace {

std::uint64_t method_bit(http::verb method)
{
  const auto value = static_cast<unsigned>(method);
  return value < 64U ? (std::uint64_t{1} << value) : 0;
}

std::string make_allow_header(std::uint64_t methods)
{
  if ((methods & method_bit(http::verb::get)) != 0) methods |= method_bit(http::verb::head);
  methods |= method_bit(http::verb::options);

  std::string result;
  for (const auto method : {
           http::verb::delete_,  http::verb::get,        http::verb::head,       http::verb::post,
           http::verb::put,      http::verb::connect,    http::verb::options,    http::verb::trace,
           http::verb::copy,     http::verb::lock,       http::verb::mkcol,      http::verb::move,
           http::verb::propfind, http::verb::proppatch,  http::verb::search,     http::verb::unlock,
           http::verb::bind,     http::verb::rebind,     http::verb::unbind,     http::verb::acl,
           http::verb::report,   http::verb::mkactivity, http::verb::checkout,   http::verb::merge,
           http::verb::msearch,  http::verb::notify,     http::verb::subscribe,  http::verb::unsubscribe,
           http::verb::patch,    http::verb::purge,      http::verb::mkcalendar, http::verb::link,
           http::verb::unlink,
       }) {
    if ((methods & method_bit(method)) == 0) continue;
    if (!result.empty()) result += ", ";
    result += http::to_string(method);
  }
  return result;
}

const dispatch_index* find_index(const std::vector<dispatch_index>& indexes, std::optional<http::verb> method) noexcept
{
  const auto found =
      std::find_if(indexes.begin(), indexes.end(), [method](const auto& value) { return value.method == method; });
  return found == indexes.end() ? nullptr : &*found;
}

struct candidate_cursor final {
  struct path_segments final {
    std::array<std::string_view, settings::maximum_path_segments> values{};
    std::size_t size{};
    bool valid{true};
    bool too_many{};
  };

  struct source final {
    const dispatch_node* root{};
  };

  explicit candidate_cursor(const path_segments& path_value) noexcept
    : path{&path_value}
  {}

  void add(const dispatch_index* index) noexcept
  {
    if (index) sources[size++] = {&index->root};
  }

  [[nodiscard]] bool take(std::size_t& result) noexcept
  {
    result = std::numeric_limits<std::size_t>::max();
    const auto first = started ? current + 1 : std::size_t{};
    if (started && first == 0) return false;
    for (std::size_t i = 0; i < size; ++i)
      find_next(*sources[i].root, 0, first, result);
    if (result == std::numeric_limits<std::size_t>::max()) return false;
    current = result;
    started = true;
    return true;
  }

private:
  void find_next(const dispatch_node& node, std::size_t segment, std::size_t first, std::size_t& result) const noexcept
  {
    const auto found = std::lower_bound(node.candidates.begin(), node.candidates.end(), first);
    if (found != node.candidates.end()) result = std::min(result, *found);
    if (!path->valid || segment == path->size) return;

    const auto literal = node.literals.find(path->values[segment]);
    if (literal != node.literals.end()) find_next(literal->second, segment + 1, first, result);
    if (node.variable) find_next(*node.variable, segment + 1, first, result);
  }

  const path_segments* path;
  std::array<source, 3> sources{};
  std::size_t size{};
  std::size_t current{};
  bool started{};
};

candidate_cursor::path_segments split_path(std::string_view path) noexcept
{
  candidate_cursor::path_segments result;
  if (path.empty() || path.front() != '/') {
    result.valid = false;
    return result;
  }
  path.remove_prefix(1);
  if (!path.empty() && path.back() == '/') path.remove_suffix(1);
  while (!path.empty()) {
    if (result.size == result.values.size()) {
      result.valid = false;
      result.too_many = true;
      return result;
    }
    const auto slash = path.find('/');
    const auto segment = path.substr(0, slash);
    if (segment.empty()) {
      result.valid = false;
      return result;
    }
    result.values[result.size++] = segment;
    path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
  }
  return result;
}

} // namespace

application_impl::application_impl(settings config, std::vector<compiled_layer> layers)
  : m_settings{std::move(config)}
  , m_layers{std::move(layers)}
{
  for (std::size_t layer_index = 0; layer_index < m_layers.size(); ++layer_index) {
    const auto& layer = m_layers[layer_index];
    auto found = std::find_if(m_indexes.begin(), m_indexes.end(),
                              [&layer](const auto& value) { return value.method == layer.method; });
    if (found == m_indexes.end()) {
      m_indexes.push_back({});
      found = std::prev(m_indexes.end());
      found->method = layer.method;
    }

    auto* node = &found->root;
    if (layer.pattern) {
      const auto count = layer.pattern->indexable_segment_count();
      for (std::size_t segment = 0; segment < count; ++segment) {
        const auto literal = layer.pattern->indexable_literal(segment);
        if (literal) {
          node = &node->literals[std::string{*literal}];
        } else {
          if (!node->variable) node->variable = std::make_unique<dispatch_node>();
          node = node->variable.get();
        }
      }
    }
    node->candidates.push_back(layer_index);
  }
}

class dispatcher final : public std::enable_shared_from_this<dispatcher> {
public:
  dispatcher(const application_impl& application, request_state& state)
    : m_application{application}
    , m_layers{application.layers()}
    , m_path{}
    , m_candidates{m_path}
    , m_primary_cursor{m_path}
    , m_primary_next{continuation_access::make(this, &run_primary_next)}
  {
    reset(state);
  }

  void reset(request_state& state)
  {
    if (m_primary_completion) throw std::logic_error{"cannot reuse an active HTTP dispatcher"};
    m_state = &state;
    m_ctx.emplace(state);
    m_path = split_path(m_ctx->target().path);
    m_candidates = candidate_cursor{m_path};
    m_primary_cursor = candidate_cursor{m_path};
    m_primary_saved_params = {};
    m_primary_saved_route = {};
    m_primary_completion_requested.store(false, std::memory_order_relaxed);
    continuation_access::reset(m_primary_next, this, &run_primary_next);

    const auto requested = m_ctx->method();
    m_candidates.add(find_index(m_application.indexes(), std::nullopt));
    m_candidates.add(find_index(m_application.indexes(), requested));
    if (requested == http::verb::head) m_candidates.add(find_index(m_application.indexes(), http::verb::get));
  }

  void execute(completion_callback handler)
  {
    run_primary(m_candidates, std::move(handler));
  }

private:
  struct invocation final : public std::enable_shared_from_this<invocation> {
    invocation(std::shared_ptr<dispatcher> owner_value, candidate_cursor cursor_value, route_params params,
               std::string_view route, completion_handler handler)
      : owner{std::move(owner_value)}
      , cursor{std::move(cursor_value)}
      , saved_params{std::move(params)}
      , saved_route{route}
      , completion{std::move(handler)}
      , next{continuation_access::make(this, &run_next)}
    {}

    static void run_next(void* value, completion_handler handler)
    {
      auto& self = *static_cast<invocation*>(value);
      if (self.owner->m_state->websocket_upgraded)
        throw std::logic_error{"cannot continue HTTP middleware after a WebSocket upgrade"};
      std::shared_ptr<invocation> keep_alive = self.shared_from_this();
      auto completion =
          retain_completion(self.owner->m_state->io.get_executor(), std::move(handler), std::move(keep_alive));
      self.owner->run_async(self.cursor, completion_handler{std::move(completion)});
    }

    void handler_completed(boost::system::error_code ec)
    {
      {
        std::lock_guard lock{mutex};
        if (completion_requested) return;
        completion_requested = true;
      }
      auto allocator = boost::asio::get_associated_allocator(*completion);
      auto self = shared_from_this();
      boost::asio::post(owner->m_state->io.get_executor(),
                        boost::asio::bind_allocator(
                            allocator, [self = std::move(self), ec]() mutable { self->deliver_completion(ec); }));
    }

    [[nodiscard]] const completion_handler& associated_completion() const noexcept
    {
      return *completion;
    }

    void deliver_completion(boost::system::error_code ec)
    {
      std::optional<completion_handler> completed;
      {
        std::lock_guard lock{mutex};
        completed.emplace(std::move(*completion));
        completion.reset();
        owner->m_state->route_values = std::move(saved_params);
        owner->m_state->current_route = saved_route;
      }
      post_completion(owner->m_state->io.get_executor(), std::move(*completed), ec);
    }

    std::shared_ptr<dispatcher> owner;
    candidate_cursor cursor;
    route_params saved_params;
    std::string_view saved_route;
    std::optional<completion_handler> completion;
    continuation next;
    std::mutex mutex;
    bool completion_requested{};
  };

  static void run_primary_next(void* value, completion_handler handler)
  {
    auto& self = *static_cast<dispatcher*>(value);
    if (self.m_state->websocket_upgraded)
      throw std::logic_error{"cannot continue HTTP middleware after a WebSocket upgrade"};
    auto keep_alive = self.shared_from_this();
    auto completion = retain_completion(self.m_state->io.get_executor(), std::move(handler), std::move(keep_alive));
    self.run_async(self.m_primary_cursor, completion_handler{std::move(completion)});
  }

  void primary_handler_completed(boost::system::error_code ec)
  {
    if (m_primary_completion_requested.exchange(true, std::memory_order_acq_rel)) return;
    auto self = shared_from_this();
    boost::asio::dispatch(m_state->io.get_executor(),
                          [self = std::move(self), ec]() mutable { self->deliver_primary_completion(ec); });
  }

  void deliver_primary_completion(boost::system::error_code ec)
  {
    std::optional<completion_callback> completed{std::move(*m_primary_completion)};
    m_primary_completion.reset();
    m_state->route_values = std::move(m_primary_saved_params);
    m_state->current_route = m_primary_saved_route;
    (*completed)(ec);
  }

  template<typename Handler>
  void finish_if_needed(Handler handler)
  {
    if (m_state->response != response_progress::idle) {
      if constexpr (std::is_same_v<Handler, completion_handler>)
        post_completion(m_state->io.get_executor(), std::move(handler), boost::system::error_code{});
      else
        handler({});
      return;
    }

    m_state->current_route = {};
    if (m_path.too_many) {
      m_ctx->async_send_status(http::status::uri_too_long, std::move(handler));
      return;
    }

    if (m_ctx->method() == http::verb::options && m_ctx->target().path == "*") {
      m_ctx->response_headers().set(http::field::allow, make_allow_header(find_server_methods()));
      m_ctx->async_send_status(http::status::no_content, std::move(handler));
      return;
    }

    const auto allowed_methods = find_allowed_methods();
    if (allowed_methods != 0) {
      const auto requested = m_ctx->method();
      const bool matched_requested_method =
          (allowed_methods & method_bit(requested)) != 0 ||
          (requested == http::verb::head && (allowed_methods & method_bit(http::verb::get)) != 0);
      if (matched_requested_method) {
        m_ctx->async_send_status(http::status::not_found, std::move(handler));
        return;
      }

      m_ctx->response_headers().set(http::field::allow, make_allow_header(allowed_methods));
      if (requested == http::verb::options)
        m_ctx->async_send_status(http::status::no_content, std::move(handler));
      else
        m_ctx->async_send_status(http::status::method_not_allowed, std::move(handler));
      return;
    }

    m_ctx->async_send_status(http::status::not_found, std::move(handler));
  }

  void run(candidate_cursor cursor, completion_handler handler)
  {
    std::size_t i{};
    while (cursor.take(i)) {
      const auto& layer = m_layers[i];
      route_params candidate;
      if (layer.pattern && !layer.pattern->match(m_ctx->target().path, candidate)) continue;

      route_params saved_params = m_state->route_values;
      const auto saved_route = m_state->current_route;
      m_state->route_values = candidate;
      if (layer.method && layer.pattern) m_state->current_route = layer.pattern->source();
      auto state = std::make_shared<invocation>(shared_from_this(), cursor, std::move(saved_params), saved_route,
                                                std::move(handler));
      try {
        auto completion =
            bind_associated(m_state->io.get_executor(), state->associated_completion(),
                            [state](boost::system::error_code ec) mutable { state->handler_completed(ec); });
        layer.callback->async_invoke(*m_ctx, state->next, completion_handler{std::move(completion)});
      } catch (...) {
        m_state->exception = std::current_exception();
        state->handler_completed(make_error_code(boost::system::errc::io_error));
      }
      return;
    }
    finish_if_needed(std::move(handler));
  }

  void run_primary(candidate_cursor cursor, completion_callback handler)
  {
    std::size_t i{};
    while (cursor.take(i)) {
      const auto& layer = m_layers[i];
      route_params candidate;
      if (layer.pattern && !layer.pattern->match(m_ctx->target().path, candidate)) continue;

      m_primary_cursor = cursor;
      m_primary_saved_params = m_state->route_values;
      m_primary_saved_route = m_state->current_route;
      m_primary_completion.emplace(std::move(handler));
      m_state->route_values = candidate;
      if (layer.method && layer.pattern) m_state->current_route = layer.pattern->source();
      auto self = shared_from_this();
      try {
        auto completion = boost::asio::bind_executor(
            m_state->io.get_completion_executor(),
            [self](boost::system::error_code ec) mutable { self->primary_handler_completed(ec); });
        layer.callback->async_invoke(*m_ctx, m_primary_next, completion_handler{std::move(completion)});
      } catch (...) {
        m_state->exception = std::current_exception();
        primary_handler_completed(make_error_code(boost::system::errc::io_error));
      }
      return;
    }
    finish_if_needed(std::move(handler));
  }

  void run_async(candidate_cursor cursor, completion_handler handler)
  {
    auto allocator = boost::asio::get_associated_allocator(handler);
    auto self = shared_from_this();
    boost::asio::post(m_state->io.get_executor(),
                      boost::asio::bind_allocator(allocator, [self = std::move(self), cursor = std::move(cursor),
                                                              handler = std::move(handler)]() mutable {
                        self->run(std::move(cursor), std::move(handler));
                      }));
  }

  [[nodiscard]] std::uint64_t find_allowed_methods() const
  {
    std::uint64_t result{};
    for (const auto& index : m_application.indexes()) {
      if (!index.method) continue;
      candidate_cursor candidates{m_path};
      candidates.add(&index);
      std::size_t layer_index{};
      while (candidates.take(layer_index)) {
        route_params ignored;
        const auto& pattern = m_layers[layer_index].pattern;
        if (!pattern || pattern->match(m_ctx->target().path, ignored)) {
          result |= method_bit(*index.method);
          break;
        }
      }
    }
    return result;
  }

  [[nodiscard]] std::uint64_t find_server_methods() const noexcept
  {
    std::uint64_t result{};
    for (const auto& index : m_application.indexes()) {
      if (index.method) result |= method_bit(*index.method);
    }
    return result;
  }

  const application_impl& m_application;
  const std::vector<compiled_layer>& m_layers;
  request_state* m_state{};
  std::optional<request_context> m_ctx;
  candidate_cursor::path_segments m_path;
  candidate_cursor m_candidates;
  candidate_cursor m_primary_cursor;
  route_params m_primary_saved_params;
  std::string_view m_primary_saved_route;
  std::optional<completion_callback> m_primary_completion;
  continuation m_primary_next;
  std::atomic_bool m_primary_completion_requested{};
};

void application_impl::dispatch(request_state& state, completion_callback handler) const
{
  auto operation = std::make_shared<dispatcher>(*this, state);
  operation->execute(std::move(handler));
}

void application_impl::dispatch(request_state& state, completion_callback handler,
                                std::shared_ptr<dispatcher>& reusable_dispatcher) const
{
  if (reusable_dispatcher)
    reusable_dispatcher->reset(state);
  else
    reusable_dispatcher = std::make_shared<dispatcher>(*this, state);
  reusable_dispatcher->execute(std::move(handler));
}

} // namespace serveza::web::details
