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

find_path(RE2_INCLUDE_DIR NAMES re2/re2.h)
find_library(RE2_LIBRARY NAMES re2)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RE2 REQUIRED_VARS RE2_INCLUDE_DIR RE2_LIBRARY)

if(RE2_FOUND AND NOT TARGET re2::re2)
  add_library(re2::re2 UNKNOWN IMPORTED)
  set_target_properties(re2::re2 PROPERTIES
    IMPORTED_LOCATION "${RE2_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${RE2_INCLUDE_DIR}"
  )
endif()

mark_as_advanced(RE2_INCLUDE_DIR RE2_LIBRARY)
