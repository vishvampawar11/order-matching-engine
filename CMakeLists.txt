cmake_minimum_required(VERSION 3.20)
project(order_matching_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(order_matching_engine src/main.cpp)
target_include_directories(order_matching_engine PRIVATE include)

if(MSVC)
    target_compile_options(order_matching_engine PRIVATE /W4)
else()
    target_compile_options(order_matching_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
