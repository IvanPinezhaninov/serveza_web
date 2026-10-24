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

if(TARGET absl::base)
  message(STATUS "Using Abseil targets provided by the parent project")
  return()
endif()

if(NOT SERVEZA_WEB_USE_BUNDLED_ABSEIL)
  find_package(absl REQUIRED GLOBAL)
  message(STATUS "Using system Abseil ${absl_VERSION}")
  return()
endif()

include(FetchContent)

set(ABSL_BUILD_TESTING OFF CACHE BOOL "Disable Abseil tests" FORCE)
set(ABSL_ENABLE_INSTALL ${SERVEZA_WEB_INSTALL} CACHE BOOL "Install bundled Abseil with Serveza Web" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "Use the parent C++ standard" FORCE)

# Abseil validates the active language standard while it is configured, before
# target-level compile features from serveza_web can affect it.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(serveza_web_abseil_exclude_from_all)
if(NOT SERVEZA_WEB_INSTALL)
  set(serveza_web_abseil_exclude_from_all EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(
  serveza_web_abseil
  URL https://github.com/abseil/abseil-cpp/archive/refs/tags/${SERVEZA_WEB_BUNDLED_ABSEIL_VERSION}.tar.gz
  URL_HASH SHA256=1692f77d1739bacf3f94337188b78583cf09bab7e420d2dc6c5605a4f86785a1
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  ${serveza_web_abseil_exclude_from_all}
  SYSTEM
)
FetchContent_MakeAvailable(serveza_web_abseil)

if(NOT TARGET absl::base)
  message(FATAL_ERROR "The selected Abseil dependency must provide absl::base")
endif()

message(STATUS "Using bundled Abseil ${SERVEZA_WEB_BUNDLED_ABSEIL_VERSION}")
