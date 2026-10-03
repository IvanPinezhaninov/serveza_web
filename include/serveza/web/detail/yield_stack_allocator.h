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

#ifndef SERVEZA_WEB_DETAIL_YIELD_STACK_ALLOCATOR_H
#define SERVEZA_WEB_DETAIL_YIELD_STACK_ALLOCATOR_H

#include <array>
#include <cstddef>

#include <boost/context/fixedsize_stack.hpp>

namespace serveza::web::details {

class yield_stack_allocator final {
public:
  boost::context::stack_context allocate()
  {
    return cache().allocate();
  }

  void deallocate(boost::context::stack_context& ctx) noexcept
  {
    cache().deallocate(ctx);
  }

private:
  class stack_cache final {
  public:
    ~stack_cache()
    {
      while (m_size != 0)
        m_allocator.deallocate(m_stacks[--m_size]);
    }

    boost::context::stack_context allocate()
    {
#if defined(BOOST_USE_VALGRIND)
      return m_allocator.allocate();
#else
      if (m_size != 0) return m_stacks[--m_size];
      return m_allocator.allocate();
#endif
    }

    void deallocate(boost::context::stack_context& ctx) noexcept
    {
#if defined(BOOST_USE_VALGRIND)
      m_allocator.deallocate(ctx);
#else
      if (m_size != m_stacks.size()) {
        m_stacks[m_size++] = ctx;
        return;
      }
      m_allocator.deallocate(ctx);
#endif
    }

  private:
    static constexpr std::size_t capacity = 64;

    boost::context::fixedsize_stack m_allocator;
    std::array<boost::context::stack_context, capacity> m_stacks{};
    std::size_t m_size{};
  };

  static stack_cache& cache()
  {
    thread_local stack_cache value;
    return value;
  }
};

} // namespace serveza::web::details

#endif // SERVEZA_WEB_DETAIL_YIELD_STACK_ALLOCATOR_H
