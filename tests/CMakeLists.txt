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

if(NOT TARGET GTest::gtest_main)
  message(FATAL_ERROR "The third-party dependency layer did not provide GTest::gtest_main")
endif()

include(GoogleTest)

function(serveza_web_configure_test_target target_name)
  set(options NO_BOOST_COROUTINE)
  cmake_parse_arguments(PARSE_ARGV 1 serveza_web_test "${options}" "" "")

  target_link_libraries(${target_name} PRIVATE serveza::web GTest::gtest_main)
  if(NOT serveza_web_test_NO_BOOST_COROUTINE)
    target_link_libraries(${target_name} PRIVATE Boost::coroutine)
  endif()
  target_compile_features(${target_name} PRIVATE cxx_std_17)
  target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  serveza_web_enable_developer_options(${target_name})

  if((SERVEZA_WEB_ENABLE_SANITIZERS OR
      SERVEZA_WEB_ENABLE_THREAD_SANITIZER OR
      SERVEZA_WEB_ENABLE_COVERAGE) AND
     CMAKE_VERSION VERSION_GREATER_EQUAL 3.18)
    gtest_discover_tests(${target_name}
                         DISCOVERY_MODE PRE_TEST
                         NO_PRETTY_VALUES
                         ${serveza_web_test_UNPARSED_ARGUMENTS})
  else()
    gtest_discover_tests(${target_name} NO_PRETTY_VALUES ${serveza_web_test_UNPARSED_ARGUMENTS})
  endif()
endfunction()

add_subdirectory(Compile)
add_subdirectory(Unit)

set(serveza_web_all_test_targets ServezaWebHeaderTests ServezaWebUnitTests)

if(SERVEZA_WEB_BUILD_INTEGRATION_TESTS)
  add_subdirectory(Integration)
  list(APPEND serveza_web_all_test_targets ServezaWebIntegrationTests ServezaWebAsyncStylesTests)
  if(SERVEZA_WEB_BUILD_CPP20)
    list(APPEND serveza_web_all_test_targets ServezaWebCoroutineIntegrationTests)
  endif()
  if(SERVEZA_WEB_USE_SSL)
    list(APPEND serveza_web_all_test_targets ServezaWebHttpsIntegrationTests)
  endif()
endif()

add_custom_target(
  ServezaWebCheck
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  DEPENDS ${serveza_web_all_test_targets}
  USES_TERMINAL
)
