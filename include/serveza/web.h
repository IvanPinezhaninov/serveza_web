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

#ifndef SERVEZA_WEB_H
#define SERVEZA_WEB_H

// IWYU pragma: begin_exports
#include <serveza/web/application.h>
#include <serveza/web/async.h>
#include <serveza/web/body.h>
#include <serveza/web/context.h>
#include <serveza/web/cookies.h>
#include <serveza/web/errors.h>
#include <serveza/web/layer.h>
#include <serveza/web/middleware.h>
#include <serveza/web/middleware/access_log.h>
#include <serveza/web/middleware/cors.h>
#include <serveza/web/middleware/recover.h>
#include <serveza/web/middleware/request_body_limit.h>
#include <serveza/web/middleware/request_id.h>
#include <serveza/web/middleware/security_headers.h>
#include <serveza/web/middleware/static_files.h>
#include <serveza/web/middleware/traffic_observer.h>
#include <serveza/web/middleware/websocket_upgrade.h>
#include <serveza/web/route_params.h>
#include <serveza/web/router.h>
#include <serveza/web/session.h>
#include <serveza/web/settings.h>
#include <serveza/web/storage.h>
#include <serveza/web/target.h>
#include <serveza/web/websocket.h>
// IWYU pragma: end_exports

#endif // SERVEZA_WEB_H
