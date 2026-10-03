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

function(serveza_web_add_coverage_target)
  find_program(GCOVR_EXECUTABLE NAMES gcovr REQUIRED)

  set(serveza_web_coverage_directory "${PROJECT_BINARY_DIR}/coverage")
  set(serveza_web_coverage_inputs
    "${PROJECT_BINARY_DIR}/CMakeFiles/serveza_web.dir"
    "${PROJECT_BINARY_DIR}/tests/Unit/CMakeFiles/ServezaWebUnitTests.dir"
    "${PROJECT_BINARY_DIR}/tests/Integration/Http/CMakeFiles/ServezaWebIntegrationTests.dir"
    "${PROJECT_BINARY_DIR}/tests/Integration/AsyncStyles/CMakeFiles/ServezaWebAsyncStylesTests.dir"
  )
  set(serveza_web_coverage_targets
    ServezaWebAsyncStylesTests
    ServezaWebHeaderTests
    ServezaWebIntegrationTests
    ServezaWebUnitTests
  )

  if(TARGET ServezaWebCoroutineIntegrationTests)
    list(APPEND serveza_web_coverage_inputs
      "${PROJECT_BINARY_DIR}/tests/Integration/Coroutine/CMakeFiles/ServezaWebCoroutineIntegrationTests.dir"
    )
    list(APPEND serveza_web_coverage_targets ServezaWebCoroutineIntegrationTests)
  endif()

  add_custom_target(
    ServezaWebCoverage
    COMMAND ${CMAKE_COMMAND}
            -DCOVERAGE_BUILD_DIRECTORY="${PROJECT_BINARY_DIR}"
            -P "${PROJECT_SOURCE_DIR}/cmake/ClearCoverageData.cmake"
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${PROJECT_BINARY_DIR}" --output-on-failure -C $<CONFIG>
    COMMAND ${CMAKE_COMMAND} -E make_directory "${serveza_web_coverage_directory}"
    COMMAND ${GCOVR_EXECUTABLE}
            --root "${PROJECT_SOURCE_DIR}"
            ${serveza_web_coverage_inputs}
            --filter "^src/"
            --filter "^include/serveza/web/"
            --merge-lines
            --gcov-ignore-parse-errors=negative_hits.warn_once_per_file
            --print-summary
            --fail-under-line 95
            --txt "${serveza_web_coverage_directory}/summary.txt"
            --xml "${serveza_web_coverage_directory}/coverage.xml"
            --html-details "${serveza_web_coverage_directory}/index.html"
    DEPENDS ${serveza_web_coverage_targets}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Generating Serveza Web coverage report (minimum 95% lines)"
    USES_TERMINAL
  )
endfunction()
