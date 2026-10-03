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
   NOT DEFINED OUTPUT_FILE OR NOT DEFINED PACKAGE_VERSION)
  message(FATAL_ERROR "source archive parameters are incomplete")
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
  message(FATAL_ERROR "tracked changes must be committed before creating a source archive")
endif()

get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

execute_process(
  COMMAND "${GIT_EXECUTABLE}" archive
          --format=tar.gz
          "--prefix=serveza_web-${PACKAGE_VERSION}/"
          "--output=${OUTPUT_FILE}"
          HEAD
  WORKING_DIRECTORY "${SOURCE_DIRECTORY}"
  RESULT_VARIABLE archive_result
  ERROR_VARIABLE archive_error
)
if(NOT archive_result EQUAL 0)
  message(FATAL_ERROR "could not create source archive: ${archive_error}")
endif()

file(SHA256 "${OUTPUT_FILE}" archive_hash)
get_filename_component(archive_name "${OUTPUT_FILE}" NAME)
file(WRITE "${OUTPUT_FILE}.sha256" "${archive_hash}  ${archive_name}\n")

message(STATUS "Created ${OUTPUT_FILE}")
message(STATUS "SHA256 ${archive_hash}")
