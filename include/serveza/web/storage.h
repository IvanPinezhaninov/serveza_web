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

#ifndef SERVEZA_WEB_STORAGE_H
#define SERVEZA_WEB_STORAGE_H

#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include <boost/any/unique_any.hpp>

namespace serveza::web {

/**
 * @brief Type-indexed request-local object store.
 *
 * At most one value exists for each unqualified type. Owned values are
 * destroyed with the storage; attached values remain owned by their caller.
 */
class storage final {
public:
  /** @brief Constructs and stores an owned @p T, replacing an existing value of that type. */
  template<typename T, typename... Args>
  T& emplace(Args&&... args)
  {
    static_assert(!std::is_reference_v<T> && !std::is_pointer_v<T>, "storage owns object values");
    auto [it, inserted] =
        m_values.insert_or_assign(std::type_index{typeid(T)},
                                  boost::anys::unique_any{boost::anys::in_place_type<T>, std::forward<Args>(args)...});
    (void)inserted;
    return *boost::anys::any_cast<T>(&it->second);
  }

  /**
   * @brief Attaches a non-owning reference to @p value.
   *
   * The referenced object must outlive this storage. Constness is preserved
   * when retrieving the attachment.
   */
  template<typename T>
  void attach(T& value)
  {
    using value_type = std::remove_cv_t<T>;
    using pointer_type = std::conditional_t<std::is_const_v<T>, const value_type*, value_type*>;
    m_values.insert_or_assign(std::type_index{typeid(value_type)},
                              boost::anys::unique_any{static_cast<pointer_type>(&value)});
  }

  /** @brief Returns the stored mutable @p T, or @c nullptr when absent or const-attached. */
  template<typename T>
  [[nodiscard]] T* get() noexcept
  {
    using value_type = std::remove_cv_t<T>;
    const auto it = m_values.find(std::type_index{typeid(value_type)});
    if (it == m_values.end()) return nullptr;
    if (auto* owned = boost::anys::any_cast<value_type>(&it->second)) return owned;
    if (auto** attached = boost::anys::any_cast<value_type*>(&it->second)) return *attached;
    return nullptr;
  }

  /** @brief Returns the stored @p T, or @c nullptr when absent. */
  template<typename T>
  [[nodiscard]] const T* get() const noexcept
  {
    using value_type = std::remove_cv_t<T>;
    const auto it = m_values.find(std::type_index{typeid(value_type)});
    if (it == m_values.end()) return nullptr;
    if (const auto* owned = boost::anys::any_cast<value_type>(&it->second)) return owned;
    if (auto* const* attached = boost::anys::any_cast<value_type*>(&it->second)) return *attached;
    if (const auto* const* attached = boost::anys::any_cast<const value_type*>(&it->second)) return *attached;
    return nullptr;
  }

  /** @brief Returns the stored mutable @p T, throwing @c std::out_of_range when absent. */
  template<typename T>
  T& require()
  {
    if (auto* value = get<T>()) return *value;
    throw std::out_of_range{"request storage object is missing"};
  }

  /** @brief Returns the stored @p T, throwing @c std::out_of_range when absent. */
  template<typename T>
  const T& require() const
  {
    if (const auto* value = get<T>()) return *value;
    throw std::out_of_range{"request storage object is missing"};
  }

  /** @brief Removes the stored value or attachment of type @p T when present. */
  template<typename T>
  void erase() noexcept
  {
    m_values.erase(std::type_index{typeid(std::remove_cv_t<T>)});
  }

private:
  std::unordered_map<std::type_index, boost::anys::unique_any> m_values;
};

} // namespace serveza::web

#endif // SERVEZA_WEB_STORAGE_H
