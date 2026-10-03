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

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS serveza_web
  EXPORT serveza_web_targets
  FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(EXPORT serveza_web_targets
  FILE serveza_webTargets.cmake
  NAMESPACE serveza::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/serveza_web
)

configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/serveza_webConfig.cmake.in"
  "${PROJECT_BINARY_DIR}/serveza_webConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/serveza_web
)

if(PROJECT_VERSION_MAJOR EQUAL 0)
  set(serveza_web_version_compatibility SameMinorVersion)
else()
  set(serveza_web_version_compatibility SameMajorVersion)
endif()

write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/serveza_webConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY ${serveza_web_version_compatibility}
)

install(FILES
  "${PROJECT_BINARY_DIR}/serveza_webConfig.cmake"
  "${PROJECT_BINARY_DIR}/serveza_webConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/serveza_web
)

install(FILES
  "${PROJECT_SOURCE_DIR}/LICENSE"
  "${PROJECT_SOURCE_DIR}/README.md"
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
