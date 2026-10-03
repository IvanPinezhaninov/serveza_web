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

cmake_minimum_required(VERSION 3.28)

project(serveza_web LANGUAGES CXX VERSION 0.1.0)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)

set(SERVEZA_WEB_BOOST_MIN_VERSION 1.83.0)
set(SERVEZA_WEB_BUNDLED_BOOST_VERSION 1.92.0)
set(SERVEZA_WEB_BUNDLED_SERVEZA_VERSION 0.1.0)

if(PROJECT_IS_TOP_LEVEL)
  option(BUILD_SHARED_LIBS "Build libraries as shared libraries" OFF)
endif()

if(PROJECT_IS_TOP_LEVEL AND WIN32 AND NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

option(SERVEZA_WEB_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})

option(SERVEZA_WEB_BUILD_EXAMPLES "Build Serveza Web examples" OFF)

option(SERVEZA_WEB_BUILD_TESTS "Build Serveza Web tests" OFF)
option(SERVEZA_WEB_BUILD_INTEGRATION_TESTS "Build network integration tests" OFF)

option(SERVEZA_WEB_BUILD_BENCHMARKS "Build routing and HTTP benchmarks" OFF)
option(SERVEZA_WEB_BUILD_FUZZERS "Build Clang/libFuzzer parser targets" OFF)

option(SERVEZA_WEB_BUILD_CPP20 "Build optional C++20 coroutine tests and examples" OFF)
option(SERVEZA_WEB_BUILD_DOCS "Build the Doxygen API reference" OFF)
option(SERVEZA_WEB_USE_SSL "Build HTTPS support" OFF)

option(SERVEZA_WEB_USE_BUNDLED_BOOST "Build pinned Boost when no parent targets exist" ON)
option(SERVEZA_WEB_USE_BUNDLED_GTEST "Build pinned GoogleTest when no parent targets exist" ON)
option(SERVEZA_WEB_USE_BUNDLED_OPENSSL "Build pinned OpenSSL when no parent targets exist" ON)

option(SERVEZA_WEB_ENABLE_COVERAGE "Instrument tests and enforce line coverage" OFF)
option(SERVEZA_WEB_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SERVEZA_WEB_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)

if(SERVEZA_WEB_BUILD_FUZZERS)
  if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "SERVEZA_WEB_BUILD_FUZZERS requires the Clang Unix driver and libFuzzer")
  endif()
  set(SERVEZA_WEB_ENABLE_SANITIZERS ON)
endif()

if(SERVEZA_WEB_ENABLE_SANITIZERS AND SERVEZA_WEB_ENABLE_THREAD_SANITIZER)
  message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be enabled together")
endif()

if(SERVEZA_WEB_ENABLE_COVERAGE AND (SERVEZA_WEB_ENABLE_SANITIZERS OR SERVEZA_WEB_ENABLE_THREAD_SANITIZER))
  message(FATAL_ERROR "Coverage and sanitizer instrumentation cannot be enabled together")
endif()

if(SERVEZA_WEB_ENABLE_COVERAGE AND
   (NOT SERVEZA_WEB_BUILD_TESTS OR NOT SERVEZA_WEB_BUILD_INTEGRATION_TESTS))
  message(FATAL_ERROR "SERVEZA_WEB_ENABLE_COVERAGE requires unit and integration tests")
endif()

if(SERVEZA_WEB_BUILD_CPP20 AND NOT cxx_std_20 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
  message(FATAL_ERROR "SERVEZA_WEB_BUILD_CPP20 requires a compiler with C++20 support")
endif()

if(NOT TARGET serveza::serveza)
  set(SERVEZA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SERVEZA_BUILD_TLS_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SERVEZA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SERVEZA_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(SERVEZA_INSTALL ${SERVEZA_WEB_INSTALL} CACHE BOOL "" FORCE)
  set(SERVEZA_USE_BUNDLED_BOOST OFF CACHE BOOL "" FORCE)
  set(SERVEZA_USE_BUNDLED_GTEST OFF CACHE BOOL "" FORCE)
  set(SERVEZA_USE_BUNDLED_OPENSSL OFF CACHE BOOL "" FORCE)
  set(SERVEZA_EXTRA_BOOST_COMPONENTS "" CACHE STRING "" FORCE)
  set(SERVEZA_ENABLE_SANITIZERS ${SERVEZA_WEB_ENABLE_SANITIZERS} CACHE BOOL "" FORCE)
  set(SERVEZA_ENABLE_THREAD_SANITIZER ${SERVEZA_WEB_ENABLE_THREAD_SANITIZER} CACHE BOOL "" FORCE)
endif()

include(ServezaWebDeveloperOptions)
if(SERVEZA_WEB_ENABLE_SANITIZERS)
  # Boost.Context exposes ASan's fiber-switch hooks only through ucontext.
  set(BOOST_CONTEXT_IMPLEMENTATION ucontext CACHE STRING
      "Boost.Context implementation (fcontext, ucontext, winfib)" FORCE)
endif()

add_subdirectory(3rdparty)

if(SERVEZA_WEB_ENABLE_SANITIZERS AND TARGET boost_context)
  # Keep the Boost.Context library ABI consistent with its consumers.
  target_compile_definitions(boost_context PUBLIC BOOST_USE_ASAN)
  target_compile_options(boost_context PRIVATE
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
  )
endif()

add_library(${PROJECT_NAME})
add_library(serveza::web ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME}
  PRIVATE
    src/application.cpp
    src/body.cpp
    src/context.cpp
    src/cookies.cpp
    src/detail/http_date.cpp
    src/errors.cpp
    src/middleware.cpp
    src/middleware/access_log.cpp
    src/middleware/cors.cpp
    src/middleware/recover.cpp
    src/middleware/request_body_limit.cpp
    src/middleware/request_id.cpp
    src/middleware/security_headers.cpp
    src/middleware/static_files.cpp
    src/middleware/traffic_observer.cpp
    src/middleware/websocket_upgrade.cpp
    src/route_pattern.cpp
    src/route_params.cpp
    src/router.cpp
    src/session.cpp
    src/target.cpp
    src/websocket.cpp
  PUBLIC
    FILE_SET public_headers TYPE HEADERS
    BASE_DIRS include
    FILES
      include/serveza/web.h
      include/serveza/web/application.h
      include/serveza/web/async.h
      include/serveza/web/awaitable.h
      include/serveza/web/body.h
      include/serveza/web/context.h
      include/serveza/web/cookies.h
      include/serveza/web/export.h
      include/serveza/web/errors.h
      include/serveza/web/layer.h
      include/serveza/web/middleware.h
      include/serveza/web/route_params.h
      include/serveza/web/router.h
      include/serveza/web/session.h
      include/serveza/web/settings.h
      include/serveza/web/middleware/access_log.h
      include/serveza/web/middleware/cors.h
      include/serveza/web/middleware/recover.h
      include/serveza/web/middleware/request_body_limit.h
      include/serveza/web/middleware/request_id.h
      include/serveza/web/middleware/security_headers.h
      include/serveza/web/middleware/static_files.h
      include/serveza/web/middleware/traffic_observer.h
      include/serveza/web/middleware/websocket_upgrade.h
      include/serveza/web/storage.h
      include/serveza/web/target.h
      include/serveza/web/websocket.h
      include/serveza/web/yield.h
      include/serveza/web/detail/layer.h
      include/serveza/web/detail/yield_stack_allocator.h
  PRIVATE
    src/detail/completion_callback.h
    src/detail/core.h
    src/detail/http_date.h
    src/detail/route_pattern.h
    src/middleware/header_utils.h
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

target_compile_definitions(${PROJECT_NAME}
  PUBLIC
    BOOST_ASIO_NO_DEPRECATED
    SERVEZA_WEB_USE_SSL=$<BOOL:${SERVEZA_WEB_USE_SSL}>
)

if(WIN32)
  target_compile_definitions(${PROJECT_NAME} PUBLIC _WIN32_WINNT=0x0A00 BOOST_ALL_NO_LIB)
endif()

target_include_directories(${PROJECT_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    src
)

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    serveza::serveza
    $<BUILD_LOCAL_INTERFACE:Boost::any>
    $<BUILD_LOCAL_INTERFACE:serveza_web_boost_beast_headers>
    $<$<BOOL:${SERVEZA_WEB_USE_SSL}>:OpenSSL::Crypto>
    $<$<BOOL:${SERVEZA_WEB_USE_SSL}>:OpenSSL::SSL>
  PRIVATE
    $<BUILD_LOCAL_INTERFACE:Boost::uuid>
)

if(PROJECT_VERSION_MAJOR EQUAL 0)
  set(serveza_web_soversion "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
else()
  set(serveza_web_soversion "${PROJECT_VERSION_MAJOR}")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  EXPORT_NAME web
  POSITION_INDEPENDENT_CODE ON
  SOVERSION ${serveza_web_soversion}
  VERSION ${PROJECT_VERSION}
  VISIBILITY_INLINES_HIDDEN ON
)

get_target_property(SERVEZA_WEB_LIBRARY_TYPE ${PROJECT_NAME} TYPE)
if(SERVEZA_WEB_LIBRARY_TYPE STREQUAL "SHARED_LIBRARY")
  target_compile_definitions(${PROJECT_NAME}
    PRIVATE SERVEZA_WEB_LIBRARY_BUILD
    INTERFACE SERVEZA_WEB_USE_SHARED
  )
endif()

serveza_web_enable_developer_options(${PROJECT_NAME})

if(SERVEZA_WEB_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)

  if(SERVEZA_WEB_ENABLE_COVERAGE)
    include(ServezaWebCoverage)
    serveza_web_add_coverage_target()
  endif()
endif()

if(SERVEZA_WEB_BUILD_FUZZERS)
  add_subdirectory(tests/Fuzz)
endif()

if(SERVEZA_WEB_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(SERVEZA_WEB_BUILD_DOCS)
  include(ServezaWebDoxygen)
  serveza_web_add_doxygen_target()
endif()

if(SERVEZA_WEB_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(SERVEZA_WEB_INSTALL)
  include(ServezaWebInstall)
endif()

if(PROJECT_IS_TOP_LEVEL)
  include(ServezaWebPackaging)
endif()
