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

if(TARGET re2::re2)
  message(STATUS "Using RE2 target provided by the parent project")
  return()
endif()

if(NOT SERVEZA_WEB_USE_BUNDLED_RE2)
  find_package(RE2 REQUIRED GLOBAL)
  message(STATUS "Using system RE2")
  return()
endif()

include(FetchContent)

set(RE2_BUILD_TESTING OFF CACHE BOOL "Disable RE2 tests and benchmarks" FORCE)
set(RE2_TEST OFF CACHE BOOL "Disable RE2 tests" FORCE)
set(RE2_BENCHMARK OFF CACHE BOOL "Disable RE2 benchmarks" FORCE)
set(RE2_INSTALL ${SERVEZA_WEB_INSTALL} CACHE BOOL "Install bundled RE2 with Serveza Web" FORCE)
set(RE2_USE_ICU OFF CACHE BOOL "Disable RE2 ICU integration" FORCE)

set(serveza_web_re2_exclude_from_all)
if(NOT SERVEZA_WEB_INSTALL)
  set(serveza_web_re2_exclude_from_all EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(
  serveza_web_re2
  URL https://github.com/google/re2/archive/refs/tags/${SERVEZA_WEB_BUNDLED_RE2_VERSION}.tar.gz
  URL_HASH SHA256=87f6029d2f6de8aa023654240a03ada90e876ce9a4676e258dd01ea4c26ffd67
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  ${serveza_web_re2_exclude_from_all}
  SYSTEM
)
FetchContent_MakeAvailable(serveza_web_re2)

if(NOT TARGET re2::re2)
  message(FATAL_ERROR "The selected RE2 dependency must provide re2::re2")
endif()

message(STATUS "Using bundled RE2 ${SERVEZA_WEB_BUNDLED_RE2_VERSION}")
