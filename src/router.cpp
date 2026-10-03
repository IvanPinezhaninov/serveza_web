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

#include <serveza/web/router.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "detail/core.h"

namespace serveza::web {

namespace {

void validate_settings(const settings& value)
{
  using seconds = std::chrono::seconds;
  if (value.request_timeout <= seconds::zero()) throw std::invalid_argument{"request timeout must be positive"};
  if (value.keep_alive_timeout <= seconds::zero()) throw std::invalid_argument{"keep-alive timeout must be positive"};
  if (value.tls_handshake_timeout <= seconds::zero())
    throw std::invalid_argument{"TLS handshake timeout must be positive"};
  if (value.tls_shutdown_timeout <= seconds::zero())
    throw std::invalid_argument{"TLS shutdown timeout must be positive"};
  if (value.header_limit == 0 || value.header_limit > std::numeric_limits<std::uint32_t>::max())
    throw std::invalid_argument{"header limit must be between 1 and UINT32_MAX"};
  if (value.body_limit > std::numeric_limits<std::size_t>::max() - value.header_limit)
    throw std::invalid_argument{"combined header and body limits are too large"};
  if (value.regex_segment_limit == 0) throw std::invalid_argument{"regex segment limit must be positive"};
}

std::string normalize_prefix(std::string prefix)
{
  if (prefix.empty()) return {};
  if (prefix.front() != '/') prefix.insert(prefix.begin(), '/');
  while (prefix.size() > 1 && prefix.back() == '/')
    prefix.pop_back();
  return prefix;
}

std::string join_paths(std::string_view prefix, std::string_view path)
{
  if (prefix.empty() || prefix == "/") return std::string{path.empty() ? "/" : path};
  if (path.empty() || path == "/") return std::string{prefix};
  std::string result{prefix};
  if (path.front() != '/') result.push_back('/');
  result.append(path);
  return result;
}

} // namespace

struct router::impl final {
  struct definition final {
    std::optional<boost::beast::http::verb> method;
    std::string path;
    match_kind kind;
    std::shared_ptr<details::layer> callback;
  };

  explicit impl(settings value)
    : config{std::move(value)}
  {}

  settings config;
  std::vector<definition> definitions;
};

router::router(settings value)
  : m_impl{std::make_unique<impl>(std::move(value))}
{
  validate_settings(m_impl->config);
}

router::~router() = default;

router::router(router&&) noexcept = default;

router& router::operator=(router&&) noexcept = default;

router& router::add(std::optional<boost::beast::http::verb> method, std::string path, match_kind kind,
                    std::shared_ptr<details::layer> callable)
{
  if (!m_impl) throw std::logic_error{"router was already consumed"};
  if (!callable) throw std::invalid_argument{"pipeline callable must not be null"};
  if (method == boost::beast::http::verb::unknown)
    throw std::invalid_argument{"unknown HTTP method cannot be registered"};
  if (kind != match_kind::any) path = normalize_prefix(std::move(path));
  m_impl->definitions.push_back({method, std::move(path), kind, std::move(callable)});
  return *this;
}

router& router::mount(std::string prefix, router child)
{
  if (!m_impl || !child.m_impl) throw std::logic_error{"cannot mount a consumed router"};
  prefix = normalize_prefix(std::move(prefix));
  if (prefix.empty()) throw std::invalid_argument{"router mount prefix must not be empty"};

  for (auto& definition : child.m_impl->definitions) {
    if (definition.kind == match_kind::any) {
      definition.path = prefix;
      definition.kind = match_kind::prefix;
    } else {
      definition.path = join_paths(prefix, definition.path);
    }
    m_impl->definitions.push_back(std::move(definition));
  }
  child.m_impl.reset();
  return *this;
}

router& router::use(std::string prefix, router child)
{
  return mount(std::move(prefix), std::move(child));
}

application router::build() &&
{
  if (!m_impl) throw std::logic_error{"router was already consumed"};

  std::vector<details::compiled_layer> layers;
  layers.reserve(m_impl->definitions.size());
  for (auto& definition : m_impl->definitions) {
    std::optional<details::compiled_pattern> pattern;
    if (definition.kind != match_kind::any) {
      const auto mode = definition.kind == match_kind::prefix ? details::compiled_pattern::mode::prefix
                                                              : details::compiled_pattern::mode::exact;
      pattern.emplace(std::move(definition.path), mode, m_impl->config);
    }
    layers.push_back({definition.method, std::move(pattern), std::move(definition.callback)});
  }

  auto implementation = std::make_shared<details::application_impl>(m_impl->config, std::move(layers));
  m_impl.reset();
  return application{std::move(implementation)};
}

} // namespace serveza::web
