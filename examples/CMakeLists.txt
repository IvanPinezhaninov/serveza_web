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

add_subdirectory(Callback)
if(SERVEZA_WEB_BUILD_CPP20)
  add_subdirectory(Coroutine)
endif()
add_subdirectory(Yield)
add_subdirectory(Bodies)
add_subdirectory(Cookies)
add_subdirectory(Metrics)
add_subdirectory(Middleware)
add_subdirectory(Routing)
add_subdirectory(StaticFiles)
add_subdirectory(TrafficObserver)
add_subdirectory(WebSocket)
