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

#include <serveza/web/body.h>

#include <exception>

#include <boost/asio/post.hpp>
#include <boost/system/errc.hpp>

#include "detail/core.h"

namespace serveza::web {

byte_view::byte_view() noexcept = default;

byte_view::byte_view(const void* data, std::size_t size) noexcept
  : m_data{static_cast<const std::byte*>(data)}
  , m_size{size}
{}

byte_view::byte_view(const byte_buffer& value) noexcept
  : byte_view{value.data(), value.size()}
{}

byte_view::byte_view(std::string_view value) noexcept
  : byte_view{value.data(), value.size()}
{}

const std::byte* byte_view::data() const noexcept
{
  return m_data;
}

std::size_t byte_view::size() const noexcept
{
  return m_size;
}

bool byte_view::empty() const noexcept
{
  return m_size == 0;
}

const std::byte* byte_view::begin() const noexcept
{
  return m_data;
}

const std::byte* byte_view::end() const noexcept
{
  return m_size == 0 ? m_data : m_data + m_size;
}

response_writer::response_writer(details::request_state& state) noexcept
  : m_state{&state}
{}

void response_writer::do_async_write(byte_view value, completion_handler handler)
{
  if (value.empty()) {
    boost::asio::post(m_state->io.get_executor(),
                      [handler = std::move(handler)]() mutable { handler(boost::system::error_code{}); });
    return;
  }
  auto completion = std::make_shared<completion_handler>(std::move(handler));
  try {
    m_state->io.async_write_chunk(value.data(), value.size(),
                                  [this, value, completion](boost::system::error_code ec) mutable {
                                    if (!ec) {
                                      details::observe_response_body(*m_state, value);
                                      m_state->response_body_size += value.size();
                                    }
                                    auto completed = std::move(*completion);
                                    completed(ec);
                                  });
  } catch (...) {
    m_state->exception = std::current_exception();
    m_state->force_close = true;
    boost::asio::post(m_state->io.get_executor(), [completion = std::move(completion)]() mutable {
      auto completed = std::move(*completion);
      completed(make_error_code(boost::system::errc::io_error));
    });
  }
}

std::size_t response_writer::bytes_written() const noexcept
{
  return m_state->response_body_size;
}

boost::asio::any_io_executor response_writer::get_executor() const
{
  return m_state->io.get_executor();
}

} // namespace serveza::web
