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

function(serveza_web_add_fuzzer target_name source)
  add_executable(${target_name} ${source})
  target_link_libraries(${target_name} PRIVATE serveza::web)
  target_compile_features(${target_name} PRIVATE cxx_std_17)
  target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  serveza_web_enable_developer_options(${target_name})
  target_compile_options(${target_name} PRIVATE -fsanitize=fuzzer)
  target_link_options(${target_name} PRIVATE -fsanitize=fuzzer)
endfunction()

add_subdirectory(Cookies)
add_subdirectory(RoutePattern)
add_subdirectory(Target)

set(serveza_web_fuzz_working_corpus "${CMAKE_CURRENT_BINARY_DIR}/corpus")

add_custom_target(
  ServezaWebFuzzSmoke
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${serveza_web_fuzz_working_corpus}/cookies"
          "${serveza_web_fuzz_working_corpus}/route_pattern"
          "${serveza_web_fuzz_working_corpus}/target"
  COMMAND ${CMAKE_COMMAND} -E env ASAN_OPTIONS=detect_leaks=0
          $<TARGET_FILE:ServezaWebFuzzCookies> -runs=10000 -timeout=5 -max_len=16384
          "${serveza_web_fuzz_working_corpus}/cookies"
          "${CMAKE_CURRENT_SOURCE_DIR}/corpus/cookies"
  COMMAND ${CMAKE_COMMAND} -E env ASAN_OPTIONS=detect_leaks=0
          $<TARGET_FILE:ServezaWebFuzzRoutePattern> -runs=10000 -timeout=5 -max_len=4096
          "${serveza_web_fuzz_working_corpus}/route_pattern"
          "${CMAKE_CURRENT_SOURCE_DIR}/corpus/route_pattern"
  COMMAND ${CMAKE_COMMAND} -E env ASAN_OPTIONS=detect_leaks=0
          $<TARGET_FILE:ServezaWebFuzzTarget> -runs=10000 -timeout=5 -max_len=16384
          "${serveza_web_fuzz_working_corpus}/target"
          "${CMAKE_CURRENT_SOURCE_DIR}/corpus/target"
  DEPENDS ServezaWebFuzzCookies ServezaWebFuzzRoutePattern ServezaWebFuzzTarget
  USES_TERMINAL
  VERBATIM
)
