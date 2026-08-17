cmake_minimum_required(VERSION 3.20)
project(order_matching_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(OME_BUILD_TESTS "Build the unit test suite" ON)

add_library(order_matching_engine_warnings INTERFACE)
if(MSVC)
    target_compile_options(order_matching_engine_warnings INTERFACE /W4)
else()
    target_compile_options(order_matching_engine_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_executable(order_matching_engine src/main.cpp)
target_include_directories(order_matching_engine PRIVATE include)
target_link_libraries(order_matching_engine PRIVATE order_matching_engine_warnings)

if(OME_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
