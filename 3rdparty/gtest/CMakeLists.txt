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

if(TARGET GTest::gtest_main)
  message(STATUS "Using GoogleTest targets provided by the parent project")
  return()
endif()
if(TARGET GTest::gtest)
  message(FATAL_ERROR "The parent project must provide both GTest::gtest and GTest::gtest_main")
endif()

if(SERVEZA_WEB_USE_BUNDLED_GTEST)
  include(FetchContent)

  set(serveza_web_gtest_version 1.17.0)
  set(INSTALL_GTEST OFF CACHE BOOL "Disable GoogleTest installation" FORCE)
  if(MSVC)
    set(gtest_force_shared_crt ON CACHE BOOL "Use the shared MSVC runtime" FORCE)
  endif()

  FetchContent_Declare(
    serveza_web_googletest
    URL https://github.com/google/googletest/releases/download/v${serveza_web_gtest_version}/googletest-${serveza_web_gtest_version}.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
    SYSTEM
  )
  FetchContent_MakeAvailable(serveza_web_googletest)
  message(STATUS "Using bundled GoogleTest ${serveza_web_gtest_version}")
else()
  find_package(GTest 1.14 REQUIRED GLOBAL)
  message(STATUS "Using system GoogleTest ${GTest_VERSION}")
endif()

if(NOT TARGET GTest::gtest_main)
  message(FATAL_ERROR "The selected GoogleTest dependency must provide GTest::gtest_main")
endif()
