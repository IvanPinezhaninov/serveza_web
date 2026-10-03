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

include_guard(GLOBAL)

if(SERVEZA_WEB_INSTALL)
  if(BUILD_SHARED_LIBS)
    set(serveza_web_package_linkage shared)
  else()
    set(serveza_web_package_linkage static)
  endif()

  string(TOLOWER "${CMAKE_SYSTEM_NAME}" serveza_web_package_system)
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" serveza_web_package_processor)

  set(CPACK_PACKAGE_NAME "serveza_web")
  set(CPACK_PACKAGE_VENDOR "Ivan Pinezhaninov")
  set(CPACK_PACKAGE_CONTACT "ivan.pinezhaninov@gmail.com")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++17 asynchronous HTTP and WebSocket library")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/IvanPinezhaninov/serveza_web")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_FILE_NAME
      "serveza_web-${PROJECT_VERSION}-${serveza_web_package_system}-${serveza_web_package_processor}-${serveza_web_package_linkage}")
  set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")
  set(CPACK_PACKAGE_CHECKSUM SHA256)
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "serveza_web")
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/serveza_web")
  else()
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
  endif()
  set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")

  set(CPACK_GENERATOR TGZ ZIP)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CPACK_GENERATOR DEB RPM)

    set(CPACK_DEBIAN_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.deb")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION libdevel)
    set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "cmake (>= 3.28), libboost-dev (>= 1.83.0)")
    if(SERVEZA_WEB_USE_SSL)
      string(APPEND CPACK_DEBIAN_PACKAGE_DEPENDS ", libssl-dev")
    endif()
    if(BUILD_SHARED_LIBS)
      set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    endif()

    set(CPACK_RPM_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.rpm")
    set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT AND BSL-1.0")
    set(CPACK_RPM_PACKAGE_REQUIRES "cmake >= 3.28, boost-devel >= 1.83.0")
    if(SERVEZA_WEB_USE_SSL)
      string(APPEND CPACK_RPM_PACKAGE_REQUIRES ", openssl-devel")
    endif()
    set(CPACK_RPM_SPEC_MORE_DEFINE
        "%define _buildhost reproducible\n%define use_source_date_epoch_as_buildtime 1\n%define clamp_mtime_to_source_date_epoch 1")
  endif()

  include(CPack)
endif()

find_package(Git QUIET)
if(Git_FOUND)
  set(serveza_web_source_archive
      "${PROJECT_BINARY_DIR}/packages/serveza_web-${PROJECT_VERSION}.tar.gz")

  add_custom_target(
    ServezaWebSourcePackage
    COMMAND ${CMAKE_COMMAND}
            -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
            -DSOURCE_DIRECTORY=${PROJECT_SOURCE_DIR}
            -DOUTPUT_FILE=${serveza_web_source_archive}
            -DPACKAGE_VERSION=${PROJECT_VERSION}
            -P ${PROJECT_SOURCE_DIR}/cmake/CreateSourceArchive.cmake
    BYPRODUCTS
      ${serveza_web_source_archive}
      ${serveza_web_source_archive}.sha256
    COMMENT "Creating tracked Serveza Web source archive"
    VERBATIM
  )

  add_custom_target(
    ServezaWebVerifyRelease
    COMMAND ${CMAKE_COMMAND}
            -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
            -DSOURCE_DIRECTORY=${PROJECT_SOURCE_DIR}
            -DPACKAGE_VERSION=${PROJECT_VERSION}
            -P ${PROJECT_SOURCE_DIR}/cmake/VerifyRelease.cmake
    COMMENT "Verifying the Serveza Web release tag and metadata"
    VERBATIM
  )
endif()
