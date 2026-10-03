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

add_executable(ServezaWebHttpBenchmark src/main.cpp)
target_link_libraries(ServezaWebHttpBenchmark PRIVATE serveza::web Boost::coroutine)
target_compile_features(ServezaWebHttpBenchmark PRIVATE cxx_std_17)
serveza_web_enable_developer_options(ServezaWebHttpBenchmark)

if(SERVEZA_WEB_BUILD_CPP20)
  add_executable(ServezaWebHttpCpp20Benchmark src/main.cpp)
  target_link_libraries(ServezaWebHttpCpp20Benchmark PRIVATE serveza::web Boost::coroutine)
  target_compile_features(ServezaWebHttpCpp20Benchmark PRIVATE cxx_std_20)
  target_compile_definitions(ServezaWebHttpCpp20Benchmark PRIVATE SERVEZA_WEB_HTTP_CPP20=1)
  serveza_web_enable_developer_options(ServezaWebHttpCpp20Benchmark)
endif()
