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

if(TARGET serveza::serveza)
  message(STATUS "Using Serveza target provided by the parent project")
  return()
endif()

include(FetchContent)

FetchContent_Declare(
  serveza
  GIT_REPOSITORY https://github.com/IvanPinezhaninov/serveza.git
  GIT_TAG v${SERVEZA_WEB_BUNDLED_SERVEZA_VERSION}
  SYSTEM
)

FetchContent_MakeAvailable(serveza)

if(NOT TARGET serveza::serveza)
  message(FATAL_ERROR "The selected Serveza dependency must provide serveza::serveza")
endif()

message(STATUS
        "Using Serveza ${SERVEZA_WEB_BUNDLED_SERVEZA_VERSION} from IvanPinezhaninov/serveza")
