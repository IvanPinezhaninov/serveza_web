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

#include <serveza/web/route_params.h>

#include <stdexcept>

namespace serveza::web {

std::size_t route_params::size() const noexcept
{
  return m_size;
}

bool route_params::empty() const noexcept
{
  return m_size == 0;
}

route_params::const_iterator route_params::begin() const noexcept
{
  return m_values.data();
}

route_params::const_iterator route_params::end() const noexcept
{
  return m_values.data() + m_size;
}

std::optional<std::string_view> route_params::find(std::string_view name) const noexcept
{
  for (const auto& value : *this)
    if (value.name == name) return value.value;
  return std::nullopt;
}

std::string_view route_params::at(std::string_view name) const
{
  const auto value = find(name);
  if (!value) throw std::out_of_range{"route parameter is missing"};
  return *value;
}

} // namespace serveza::web
