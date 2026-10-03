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

set(serveza_web_boost_components any asio beast container uuid)
set(serveza_web_boost_bundled_components
    align
    any
    assert
    bind
    config
    container
    container_hash
    core
    endian
    intrusive
    logic
    mp11
    optional
    smart_ptr
    static_string
    system
    throw_exception
    type_index
    type_traits
    uuid
    winapi)
if(SERVEZA_WEB_BUILD_EXAMPLES OR SERVEZA_WEB_BUILD_TESTS OR SERVEZA_WEB_BUILD_BENCHMARKS)
  list(APPEND serveza_web_boost_components coroutine)
  list(APPEND serveza_web_boost_bundled_components coroutine)
endif()

set(serveza_web_boost_known_targets headers boost asio_core ${serveza_web_boost_components})
set(serveza_web_boost_found_in_tree OFF)
foreach(serveza_web_boost_target IN LISTS serveza_web_boost_known_targets)
  if(TARGET Boost::${serveza_web_boost_target})
    set(serveza_web_boost_found_in_tree ON)
  endif()
endforeach()

if(serveza_web_boost_found_in_tree)
  message(STATUS "Using Boost targets provided by the parent project")
elseif(SERVEZA_WEB_USE_BUNDLED_BOOST)
  include(FetchContent)

  # Asio and Beast are header-only here. Configuring their aggregate Boost
  # targets would recursively add Boost.Coroutine and Boost.Context even when
  # no stackful adapter is built.
  set(BOOST_INCLUDE_LIBRARIES ${serveza_web_boost_bundled_components})
  set(BOOST_SKIP_INSTALL_RULES ON)
  set(BOOST_INSTALL_LAYOUT system)
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)

  FetchContent_Declare(
    serveza_web_boost
    URL https://github.com/boostorg/boost/releases/download/boost-${SERVEZA_WEB_BUNDLED_BOOST_VERSION}/boost-${SERVEZA_WEB_BUNDLED_BOOST_VERSION}-cmake.tar.xz
    URL_HASH SHA256=9bed76128d4e46755dbe818487788c6fceb6f72b378f4daa49b7e1e600d9088d
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
    SYSTEM
  )
  FetchContent_MakeAvailable(serveza_web_boost)
  message(STATUS "Using bundled Boost ${SERVEZA_WEB_BUNDLED_BOOST_VERSION}")

  add_library(serveza_web_boost_asio_core INTERFACE)
  add_library(Boost::asio_core ALIAS serveza_web_boost_asio_core)
  add_library(Boost::asio ALIAS serveza_web_boost_asio_core)
  target_include_directories(serveza_web_boost_asio_core
    SYSTEM INTERFACE "${serveza_web_boost_SOURCE_DIR}/libs/asio/include")
  target_link_libraries(serveza_web_boost_asio_core
    INTERFACE
      Boost::align
      Boost::assert
      Boost::config
      Boost::system
      Boost::throw_exception
  )

  add_library(serveza_web_boost_beast INTERFACE)
  add_library(Boost::beast ALIAS serveza_web_boost_beast)
  target_include_directories(serveza_web_boost_beast
    SYSTEM INTERFACE "${serveza_web_boost_SOURCE_DIR}/libs/beast/include")
  target_link_libraries(serveza_web_boost_beast
    INTERFACE
      Boost::asio_core
      Boost::assert
      Boost::bind
      Boost::config
      Boost::container
      Boost::container_hash
      Boost::core
      Boost::endian
      Boost::intrusive
      Boost::logic
      Boost::mp11
      Boost::optional
      Boost::smart_ptr
      Boost::static_string
      Boost::system
      Boost::throw_exception
      Boost::type_index
      Boost::type_traits
      Boost::winapi
  )

  if(SERVEZA_WEB_ENABLE_THREAD_SANITIZER AND TARGET boost_context)
    target_compile_definitions(boost_context PUBLIC BOOST_USE_TSAN)
  endif()
else()
  set(serveza_web_boost_system_components ${serveza_web_boost_components})
  list(REMOVE_ITEM serveza_web_boost_system_components any asio beast container uuid)
  find_package(Boost ${SERVEZA_WEB_BOOST_MIN_VERSION} REQUIRED GLOBAL
               COMPONENTS ${serveza_web_boost_system_components})
  message(STATUS "Using system Boost ${Boost_VERSION}")
endif()

if(TARGET Boost::headers)
  set(serveza_web_dependency_boost_headers_target Boost::headers)
elseif(TARGET Boost::boost)
  set(serveza_web_dependency_boost_headers_target Boost::boost)
elseif(TARGET Boost::asio_core)
  set(serveza_web_dependency_boost_headers_target Boost::asio_core)
elseif(TARGET Boost::asio)
  set(serveza_web_dependency_boost_headers_target Boost::asio)
else()
  message(FATAL_ERROR "The selected Boost dependency must provide Boost headers")
endif()

foreach(serveza_web_boost_header_component any asio beast container uuid)
  if(serveza_web_boost_header_component IN_LIST serveza_web_boost_components AND
     NOT TARGET Boost::${serveza_web_boost_header_component})
    add_library(Boost::${serveza_web_boost_header_component} INTERFACE IMPORTED GLOBAL)
    set_property(TARGET Boost::${serveza_web_boost_header_component}
                 PROPERTY INTERFACE_LINK_LIBRARIES ${serveza_web_dependency_boost_headers_target})
  endif()
endforeach()

foreach(serveza_web_required_boost_component IN LISTS serveza_web_boost_components)
  if(NOT TARGET Boost::${serveza_web_required_boost_component})
    message(FATAL_ERROR
            "The selected Boost dependency must provide Boost::${serveza_web_required_boost_component}")
  endif()
endforeach()

# Boost::beast links the aggregate Boost::asio target, which also links stackful
# coroutine support. Replace that dependency with the header-only core target.
add_library(serveza_web_boost_beast_headers INTERFACE)
if(TARGET boost_beast AND TARGET Boost::asio_core)
  get_target_property(serveza_web_boost_beast_include_dirs
                      boost_beast INTERFACE_INCLUDE_DIRECTORIES)
  get_target_property(serveza_web_boost_beast_dependencies
                      boost_beast INTERFACE_LINK_LIBRARIES)
  list(REMOVE_ITEM serveza_web_boost_beast_dependencies Boost::asio)
  list(APPEND serveza_web_boost_beast_dependencies Boost::asio_core)

  target_include_directories(serveza_web_boost_beast_headers
    SYSTEM INTERFACE ${serveza_web_boost_beast_include_dirs})
  target_link_libraries(serveza_web_boost_beast_headers
    INTERFACE ${serveza_web_boost_beast_dependencies})
else()
  target_link_libraries(serveza_web_boost_beast_headers INTERFACE Boost::beast)
endif()

if(WIN32 AND TARGET boost_mpl)
  target_compile_definitions(boost_mpl INTERFACE BOOST_MPL_CFG_NO_PREPROCESSED_HEADERS)
endif()
