include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(order_matching_engine_tests
    order_book_test.cpp
)
target_include_directories(order_matching_engine_tests PRIVATE ../include)
target_link_libraries(order_matching_engine_tests PRIVATE gtest_main order_matching_engine_warnings)

include(GoogleTest)
gtest_discover_tests(order_matching_engine_tests)
