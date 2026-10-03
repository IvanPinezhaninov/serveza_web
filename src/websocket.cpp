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

#include <serveza/web/websocket.h>

#include <stdexcept>

#include "detail/core.h"

namespace serveza::web {

void details::validate_websocket_options(const websocket_options& options)
{
  if (options.max_message_size == 0) throw std::invalid_argument{"WebSocket message size limit must be positive"};
  if (options.handshake_timeout <= std::chrono::seconds::zero())
    throw std::invalid_argument{"WebSocket handshake timeout must be positive"};
  if (options.idle_timeout <= std::chrono::seconds::zero())
    throw std::invalid_argument{"WebSocket idle timeout must be positive"};
  if (options.close_timeout <= std::chrono::seconds::zero())
    throw std::invalid_argument{"WebSocket close timeout must be positive"};
  if (options.max_pending_messages == 0)
    throw std::invalid_argument{"WebSocket pending message limit must be positive"};
  if (options.max_pending_bytes == 0) throw std::invalid_argument{"WebSocket pending byte limit must be positive"};
}

bool websocket_message::is_text() const noexcept
{
  return type == websocket_message_type::text;
}

std::string_view websocket_message::text() const
{
  if (!is_text()) throw std::logic_error{"binary WebSocket message has no text representation"};
  if (data.empty()) return {};
  return {reinterpret_cast<const char*>(data.data()), data.size()};
}

byte_view websocket_message::bytes() const noexcept
{
  return data;
}

websocket_sender::websocket_sender() noexcept = default;

websocket_sender::websocket_sender(std::weak_ptr<details::websocket_io> io) noexcept
  : m_io{std::move(io)}
{}

bool websocket_sender::is_open() const noexcept
{
  const auto io = m_io.lock();
  return io && io->is_open();
}

websocket_send_result websocket_sender::try_send_text(std::string value) const
{
  const auto io = m_io.lock();
  return io ? io->try_send_text(std::move(value)) : websocket_send_result::closed;
}

websocket_send_result websocket_sender::try_send_binary(byte_buffer value) const
{
  const auto io = m_io.lock();
  return io ? io->try_send_binary(std::move(value)) : websocket_send_result::closed;
}

websocket_connection::websocket_connection(std::shared_ptr<details::websocket_io> io) noexcept
  : m_io{std::move(io)}
{}

bool websocket_connection::is_open() const noexcept
{
  return m_io->is_open();
}

websocket_sender websocket_connection::sender() const noexcept
{
  return websocket_sender{m_io};
}

boost::asio::any_io_executor websocket_connection::get_executor() const
{
  return m_io->get_executor();
}

void websocket_connection::do_async_read(read_handler handler)
{
  m_io->async_read(std::move(handler));
}

void websocket_connection::do_async_write_text(std::string value, completion_handler handler)
{
  m_io->async_write_text(std::move(value), std::move(handler));
}

void websocket_connection::do_async_write_binary(byte_buffer value, completion_handler handler)
{
  m_io->async_write_binary(std::move(value), std::move(handler));
}

void websocket_connection::do_async_ping(std::string value, completion_handler handler)
{
  m_io->async_ping(std::move(value), std::move(handler));
}

void websocket_connection::do_async_close(std::uint16_t code, std::string reason, completion_handler handler)
{
  m_io->async_close(code, std::move(reason), std::move(handler));
}

} // namespace serveza::web
