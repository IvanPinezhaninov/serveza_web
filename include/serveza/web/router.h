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

#ifndef SERVEZA_WEB_ROUTER_H
#define SERVEZA_WEB_ROUTER_H

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <boost/beast/http/verb.hpp>

#include <serveza/web/application.h>
#include <serveza/web/detail/layer.h>
#include <serveza/web/export.h>
#include <serveza/web/settings.h>

namespace serveza::web {

/**
 * @brief Mutable builder for an ordered HTTP request pipeline.
 *
 * Callables are stored once while configuring the router and may later be
 * invoked concurrently by multiple sessions. Callback callables accept either
 * @c (request_context&, continuation&, completion_handler) or
 * @c (request_context&, completion_handler). A callable that accepts the
 * continuation may pass control to the next matching layer; one that omits it
 * is terminal. The registration function determines the matching scope, not
 * the callable category.
 */
class SERVEZA_WEB_API router final {
public:
  /**
   * @brief Creates an empty router using @p value as its application settings.
   * @throws std::invalid_argument if a limit or timeout is invalid.
   */
  explicit router(settings value = {});

  /** @brief Destroys this router and any callables not consumed by @ref build. */
  ~router();

  /** @brief Routers cannot be copied. */
  router(const router&) = delete;

  /** @brief Routers cannot be copy-assigned. */
  router& operator=(const router&) = delete;

  /** @brief Moves a router builder. */
  router(router&&) noexcept;

  /** @brief Move-assigns a router builder. */
  router& operator=(router&&) noexcept;

  /** @brief Appends @p callable for every request. */
  template<typename Callable>
  router& use(Callable&& callable)
  {
    return add(std::nullopt, {}, match_kind::any, details::make_layer(std::forward<Callable>(callable)));
  }

  /**
   * @brief Appends @p callable for requests below @p prefix.
   *
   * The prefix ends on a path-segment boundary, so @c /api does not match
   * @c /apiary. Captured prefix parameters are visible to the callable.
   */
  template<typename Callable>
  router& use(std::string prefix, Callable&& callable)
  {
    return add(std::nullopt, std::move(prefix), match_kind::prefix,
               details::make_layer(std::forward<Callable>(callable)));
  }

  /** @brief Appends @p callable for every request using @p method. */
  template<typename Callable>
  router& use(boost::beast::http::verb method, Callable&& callable)
  {
    return add(method, {}, match_kind::any, details::make_layer(std::forward<Callable>(callable)));
  }

  /** @brief Appends @p callable for an exact route @p path and HTTP @p method. */
  template<typename Callable>
  router& route(boost::beast::http::verb method, std::string path, Callable&& callable)
  {
    return add(method, std::move(path), match_kind::exact, details::make_layer(std::forward<Callable>(callable)));
  }

  /** @brief Appends a GET layer for @p path. */
  template<typename Callable>
  router& get(std::string path, Callable&& callable)
  {
    return route(boost::beast::http::verb::get, std::move(path), std::forward<Callable>(callable));
  }

  /** @brief Appends a method-wide GET layer. */
  template<typename Callable>
  router& get(Callable&& callable)
  {
    return use(boost::beast::http::verb::get, std::forward<Callable>(callable));
  }

  /** @brief Appends a POST layer for @p path. */
  template<typename Callable>
  router& post(std::string path, Callable&& callable)
  {
    return route(boost::beast::http::verb::post, std::move(path), std::forward<Callable>(callable));
  }

  /** @brief Appends a method-wide POST layer. */
  template<typename Callable>
  router& post(Callable&& callable)
  {
    return use(boost::beast::http::verb::post, std::forward<Callable>(callable));
  }

  /** @brief Appends a PUT layer for @p path. */
  template<typename Callable>
  router& put(std::string path, Callable&& callable)
  {
    return route(boost::beast::http::verb::put, std::move(path), std::forward<Callable>(callable));
  }

  /** @brief Appends a method-wide PUT layer. */
  template<typename Callable>
  router& put(Callable&& callable)
  {
    return use(boost::beast::http::verb::put, std::forward<Callable>(callable));
  }

  /** @brief Appends a PATCH layer for @p path. */
  template<typename Callable>
  router& patch(std::string path, Callable&& callable)
  {
    return route(boost::beast::http::verb::patch, std::move(path), std::forward<Callable>(callable));
  }

  /** @brief Appends a method-wide PATCH layer. */
  template<typename Callable>
  router& patch(Callable&& callable)
  {
    return use(boost::beast::http::verb::patch, std::forward<Callable>(callable));
  }

  /** @brief Appends a DELETE layer for @p path. */
  template<typename Callable>
  router& del(std::string path, Callable&& callable)
  {
    return route(boost::beast::http::verb::delete_, std::move(path), std::forward<Callable>(callable));
  }

  /** @brief Appends a method-wide DELETE layer. */
  template<typename Callable>
  router& del(Callable&& callable)
  {
    return use(boost::beast::http::verb::delete_, std::forward<Callable>(callable));
  }

  /** @brief Appends an OPTIONS layer for @p path. */
  template<typename Callable>
  router& options(std::string path, Callable&& callable)
  {
    return route(boost::beast::http::verb::options, std::move(path), std::forward<Callable>(callable));
  }

  /** @brief Appends a method-wide OPTIONS layer. */
  template<typename Callable>
  router& options(Callable&& callable)
  {
    return use(boost::beast::http::verb::options, std::forward<Callable>(callable));
  }

  /** @brief Mounts and consumes @p child below @p prefix. Equivalent to @ref mount. */
  router& use(std::string prefix, router child);

  /** @brief Flattens and consumes @p child below @p prefix while preserving layer order. */
  router& mount(std::string prefix, router child);

  /** @brief Consumes this router and compiles an immutable application. */
  application build() &&;

private:
  enum class match_kind { any, exact, prefix };
  struct impl;

  router& add(std::optional<boost::beast::http::verb> method, std::string path, match_kind kind,
              std::shared_ptr<details::layer> callable);

  std::unique_ptr<impl> m_impl;
};

} // namespace serveza::web

#endif // SERVEZA_WEB_ROUTER_H
