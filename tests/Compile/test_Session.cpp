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

#include <serveza/web/session.h>

#include <type_traits>

#include <boost/asio/ip/tcp.hpp>

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
#include <boost/asio/local/stream_protocol.hpp>
#endif

static_assert(std::is_same_v<serveza::web::http_session::protocol_type, boost::asio::ip::tcp>);

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
static_assert(std::is_same_v<serveza::web::local_http_session::protocol_type, boost::asio::local::stream_protocol>);
#endif
