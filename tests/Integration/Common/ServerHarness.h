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

#ifndef SERVEZA_WEB_TEST_SERVER_HARNESS_H
#define SERVEZA_WEB_TEST_SERVER_HARNESS_H

#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/system/system_error.hpp>

#include <serveza/serveza.h>

class ServerHarness final {
public:
  template<typename SessionFactory>
  explicit ServerHarness(SessionFactory&& factory)
    : ServerHarness{std::forward<SessionFactory>(factory), serveza::listener_options{}}
  {}

  template<typename SessionFactory>
  ServerHarness(SessionFactory&& factory, serveza::listener_options options)
    : m_pool{2}
    , m_server{m_pool.get_executor()}
  {
    m_listener = m_server.listen<boost::asio::ip::tcp>({boost::asio::ip::address_v4::loopback(), 0},
                                                       std::forward<SessionFactory>(factory), std::move(options));
    std::promise<boost::system::error_code> ready;
    auto future = ready.get_future();
    m_server.async_start([&ready](boost::system::error_code ec) { ready.set_value(ec); });
    const auto ec = future.get();
    if (ec) throw boost::system::system_error{ec};
  }

  ServerHarness(const ServerHarness&) = delete;
  ServerHarness& operator=(const ServerHarness&) = delete;

  ~ServerHarness()
  {
    stopNoexcept();
  }

  void stop()
  {
    if (m_stopped) return;
    std::promise<boost::system::error_code> stopped;
    auto future = stopped.get_future();
    m_server.async_wait([&stopped](boost::system::error_code ec) { stopped.set_value(ec); });
    m_server.request_stop();
    const auto ec = future.get();
    m_pool.join();
    m_stopped = true;
    if (ec) throw boost::system::system_error{ec};
  }

  [[nodiscard]] unsigned short port() const
  {
    const std::string endpoint = m_listener->endpoint();
    const auto separator = endpoint.rfind(':');
    if (separator == std::string::npos) throw std::runtime_error{"listener endpoint has no port"};
    return static_cast<unsigned short>(std::stoul(endpoint.substr(separator + 1)));
  }

  [[nodiscard]] serveza::server& server() noexcept
  {
    return m_server;
  }
  [[nodiscard]] const serveza::server& server() const noexcept
  {
    return m_server;
  }
  [[nodiscard]] const std::shared_ptr<serveza::listener>& listener() const noexcept
  {
    return m_listener;
  }

private:
  void stopNoexcept() noexcept
  {
    try {
      stop();
    } catch (...) {}
  }

  boost::asio::thread_pool m_pool;
  serveza::server m_server;
  std::shared_ptr<serveza::listener> m_listener;
  bool m_stopped{};
};

#endif // SERVEZA_WEB_TEST_SERVER_HARNESS_H
