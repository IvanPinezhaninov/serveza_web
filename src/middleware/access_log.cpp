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

#include <serveza/web/middleware/access_log.h>

#include <serveza/web/context.h>
#include <serveza/web/middleware/request_id.h>

namespace serveza::web::middleware::details {

void emit_access_log(const exchange_complete_event& event, void* sink, access_log_callback callback) noexcept
{
  try {
    access_log_entry entry;
    entry.method = event.method;
    entry.path = event.context.target().path;
    entry.route = event.route;
    entry.status = event.status;
    entry.request_body_size = event.request_body_size;
    entry.response_body_size = event.response_body_size;
    entry.response_completed = event.response_completed;
    entry.elapsed = event.elapsed;
    if (const auto* id = event.context.storage().get<request_id_value>()) entry.request_id = id->value;
    entry.listener_id = event.context.listener_id();
    if (const auto* connection = event.context.connection()) {
      entry.connection_id = connection->id;
      entry.remote_endpoint = connection->remote_endpoint;
    }
    entry.error = event.error;
    callback(sink, entry);
  } catch (...) {}
}

} // namespace serveza::web::middleware::details
