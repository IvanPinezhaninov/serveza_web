#============================================================================
#
# Copyright (C) 2026 Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>
#
# This file is part of serveza_web, which can be found at
# https://github.com/IvanPinezhaninov/serveza_web/.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
# OR OTHER DEALINGS IN THE SOFTWARE.
#
#============================================================================

set(
  serveza_web_header_test_sources
  test_Application.cpp
  test_Async.cpp
  test_Awaitable.cpp
  test_Body.cpp
  test_Context.cpp
  test_Cookies.cpp
  test_Errors.cpp
  test_Export.cpp
  test_Layer.cpp
  test_Middleware.cpp
  test_MiddlewareAccessLog.cpp
  test_MiddlewareCors.cpp
  test_MiddlewareRecover.cpp
  test_MiddlewareRequestBodyLimit.cpp
  test_MiddlewareRequestId.cpp
  test_MiddlewareSecurityHeaders.cpp
  test_MiddlewareTrafficObserver.cpp
  test_MiddlewareWebSocketUpgrade.cpp
  test_RouteParams.cpp
  test_Router.cpp
  test_ServezaWeb.cpp
  test_Session.cpp
  test_Settings.cpp
  test_StaticFiles.cpp
  test_Storage.cpp
  test_Target.cpp
  test_WebSocket.cpp
  test_Yield.cpp
)

add_library(ServezaWebHeaderTests OBJECT ${serveza_web_header_test_sources})
target_link_libraries(ServezaWebHeaderTests PRIVATE serveza::web Boost::coroutine)
target_compile_features(ServezaWebHeaderTests PRIVATE cxx_std_17)
serveza_web_enable_developer_options(ServezaWebHeaderTests)
