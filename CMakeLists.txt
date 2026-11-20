cmake_minimum_required(VERSION 3.20)
project(spectral_rt CXX)
find_package(Threads REQUIRED)
link_libraries(Threads::Threads)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(rt src/main.cpp)
add_executable(rt_tests tests/tests.cpp)
target_include_directories(rt_tests PRIVATE src)
# asserts must stay live in the test binary even in Release
target_compile_options(rt_tests PRIVATE -UNDEBUG)
