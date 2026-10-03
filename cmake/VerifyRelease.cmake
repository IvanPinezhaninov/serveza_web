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

if(NOT DEFINED GIT_EXECUTABLE OR NOT DEFINED SOURCE_DIRECTORY OR
   NOT DEFINED PACKAGE_VERSION)
  message(FATAL_ERROR "release verification parameters are incomplete")
endif()

execute_process(
  COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
  WORKING_DIRECTORY "${SOURCE_DIRECTORY}"
  RESULT_VARIABLE status_result
  OUTPUT_VARIABLE tracked_changes
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT status_result EQUAL 0)
  message(FATAL_ERROR "could not inspect the Git worktree")
endif()
if(NOT tracked_changes STREQUAL "")
  message(FATAL_ERROR "tracked changes must be committed before release verification")
endif()

set(expected_tag "v${PACKAGE_VERSION}")
execute_process(
  COMMAND "${GIT_EXECUTABLE}" tag --points-at HEAD --list "${expected_tag}"
  WORKING_DIRECTORY "${SOURCE_DIRECTORY}"
  RESULT_VARIABLE tag_result
  OUTPUT_VARIABLE matching_tag
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT tag_result EQUAL 0 OR NOT matching_tag STREQUAL expected_tag)
  message(FATAL_ERROR "HEAD must carry the ${expected_tag} release tag")
endif()

message(STATUS "Release ${expected_tag} metadata is consistent")
