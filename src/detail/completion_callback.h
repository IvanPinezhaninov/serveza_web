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

#ifndef SERVEZA_WEB_DETAIL_COMPLETION_CALLBACK_H
#define SERVEZA_WEB_DETAIL_COMPLETION_CALLBACK_H

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

#include <boost/system/error_code.hpp>

namespace serveza::web::details {

class completion_callback final {
public:
  completion_callback() noexcept = default;

  template<typename Function, typename Value = std::decay_t<Function>,
           std::enable_if_t<!std::is_same_v<Value, completion_callback>, int> = 0>
  completion_callback(Function&& function)
  {
    static_assert(std::is_invocable_r_v<void, Value&, boost::system::error_code>,
                  "completion callback must accept boost::system::error_code");
    if constexpr (fits_inline<Value>) {
      new (m_storage) Value(std::forward<Function>(function));
      m_operations = &inline_operations<Value>();
    } else {
      auto* value = new Value(std::forward<Function>(function));
      new (m_storage) Value*(value);
      m_operations = &allocated_operations<Value>();
    }
  }

  completion_callback(const completion_callback&) = delete;

  completion_callback(completion_callback&& other) noexcept
  {
    if (other.m_operations) other.m_operations->move(other, *this);
  }

  completion_callback& operator=(const completion_callback&) = delete;

  completion_callback& operator=(completion_callback&& other) noexcept
  {
    if (this == &other) return *this;
    reset();
    if (other.m_operations) other.m_operations->move(other, *this);
    return *this;
  }

  ~completion_callback()
  {
    reset();
  }

  explicit operator bool() const noexcept
  {
    return m_operations != nullptr;
  }

  void operator()(boost::system::error_code ec)
  {
    m_operations->invoke(*this, ec);
  }

private:
  static constexpr std::size_t storage_size = 4 * sizeof(void*);

  struct operations final {
    void (*invoke)(completion_callback&, boost::system::error_code);
    void (*move)(completion_callback&, completion_callback&) noexcept;
    void (*destroy)(completion_callback&) noexcept;
  };

  template<typename Value>
  static constexpr bool fits_inline = sizeof(Value) <= storage_size && alignof(Value) <= alignof(std::max_align_t) &&
                                      std::is_nothrow_move_constructible_v<Value>;

  template<typename Value>
  static Value& inline_value(completion_callback& callback) noexcept
  {
    return *std::launder(reinterpret_cast<Value*>(callback.m_storage));
  }

  template<typename Value>
  static Value*& allocated_value(completion_callback& callback) noexcept
  {
    return *std::launder(reinterpret_cast<Value**>(callback.m_storage));
  }

  template<typename Value>
  static const operations& inline_operations() noexcept
  {
    static constexpr operations value{
        [](completion_callback& callback, boost::system::error_code ec) { inline_value<Value>(callback)(ec); },
        [](completion_callback& source, completion_callback& destination) noexcept {
          new (destination.m_storage) Value(std::move(inline_value<Value>(source)));
          destination.m_operations = source.m_operations;
          inline_value<Value>(source).~Value();
          source.m_operations = nullptr;
        },
        [](completion_callback& callback) noexcept { inline_value<Value>(callback).~Value(); },
    };
    return value;
  }

  template<typename Value>
  static const operations& allocated_operations() noexcept
  {
    static constexpr operations value{
        [](completion_callback& callback, boost::system::error_code ec) { (*allocated_value<Value>(callback))(ec); },
        [](completion_callback& source, completion_callback& destination) noexcept {
          new (destination.m_storage) Value*(allocated_value<Value>(source));
          destination.m_operations = source.m_operations;
          allocated_value<Value>(source) = nullptr;
          source.m_operations = nullptr;
        },
        [](completion_callback& callback) noexcept { delete allocated_value<Value>(callback); },
    };
    return value;
  }

  void reset() noexcept
  {
    if (!m_operations) return;
    m_operations->destroy(*this);
    m_operations = nullptr;
  }

  alignas(std::max_align_t) unsigned char m_storage[storage_size]{};
  const operations* m_operations{};
};

} // namespace serveza::web::details

#endif // SERVEZA_WEB_DETAIL_COMPLETION_CALLBACK_H
