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

#ifndef SERVEZA_WEB_APPLICATION_H
#define SERVEZA_WEB_APPLICATION_H

#include <memory>

#include <serveza/web/export.h>

namespace serveza::web {

namespace details {
class application_impl;
}

class router;
class http_session;
#if SERVEZA_WEB_USE_SSL
class https_session;
#endif

/** @brief Immutable, cheaply copyable HTTP application produced by @ref router::build. */
class SERVEZA_WEB_API application final {
public:
  /** @brief Creates an empty application that cannot serve requests. */
  application() noexcept;

  /** @brief Returns whether this object contains a built application. */
  [[nodiscard]] explicit operator bool() const noexcept;

private:
  friend class router;
  friend class http_session;
#if SERVEZA_WEB_USE_SSL
  friend class https_session;
#endif

  explicit application(std::shared_ptr<const details::application_impl> impl) noexcept;

  std::shared_ptr<const details::application_impl> m_impl;
};

} // namespace serveza::web

#endif // SERVEZA_WEB_APPLICATION_H
