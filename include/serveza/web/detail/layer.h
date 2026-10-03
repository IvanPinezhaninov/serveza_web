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

#ifndef SERVEZA_WEB_DETAIL_LAYER_H
#define SERVEZA_WEB_DETAIL_LAYER_H

#include <memory>
#include <type_traits>
#include <utility>

#include <serveza/web/async.h>
#include <serveza/web/context.h>
#include <serveza/web/middleware.h>

namespace serveza::web::details {

class layer {
public:
  layer() = default;

  layer(const layer&) = delete;

  layer(layer&&) = delete;

  layer& operator=(const layer&) = delete;

  layer& operator=(layer&&) = delete;

  virtual ~layer() = default;

  virtual void async_invoke(request_context& ctx, continuation& next, completion_handler handler) = 0;
};

template<typename>
inline constexpr bool always_false_v = false;

template<typename Callable>
class layer_model final : public layer {
public:
  template<typename Value>
  explicit layer_model(Value&& callable)
    : m_callable{std::forward<Value>(callable)}
  {}

  void async_invoke(request_context& ctx, continuation& next, completion_handler completion) override
  {
    if constexpr (std::is_invocable_r_v<void, Callable&, request_context&, continuation&, completion_handler>) {
      m_callable(ctx, next, std::move(completion));
    } else if constexpr (std::is_invocable_r_v<void, Callable&, request_context&, completion_handler>) {
      m_callable(ctx, std::move(completion));
    } else {
      static_assert(always_false_v<Callable>,
                    "callback layer must accept (request_context&, continuation&, completion_handler) or "
                    "(request_context&, completion_handler); include serveza/web/yield.h or "
                    "serveza/web/awaitable.h and wrap stackful or C++20 layers explicitly");
    }
  }

private:
  Callable m_callable;
};

template<typename Callable>
std::shared_ptr<layer> make_layer(Callable&& callable)
{
  using model_type = layer_model<std::decay_t<Callable>>;
  return std::make_shared<model_type>(std::forward<Callable>(callable));
}

} // namespace serveza::web::details

#endif // SERVEZA_WEB_DETAIL_LAYER_H
