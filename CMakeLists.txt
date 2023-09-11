cmake_minimum_required(VERSION 3.14)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

project(libjitter)
include(CTest)

if (NOT TARGET logger)
    add_subdirectory(dependencies/logger)
endif()

add_library(libjitter JitterBuffer.cpp include/JitterBuffer.hh include/Packet.h)
target_include_directories(libjitter PUBLIC include)
target_link_libraries(libjitter PUBLIC cantina::logger)
target_compile_options(libjitter PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(libjitter PROPERTIES
    CXX_STANDARD 20)

add_library(clibjitter SHARED libjitter.cpp include/libjitter.h)
target_include_directories(clibjitter PUBLIC include)
target_link_libraries(clibjitter PUBLIC libjitter)
target_compile_options(clibjitter PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(clibjitter PROPERTIES
    CXX_STANDARD 20)

if (BUILD_TESTING AND LIBJITTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/doctest)
    add_compile_definitions(LIBJITTER_BUILD_TESTS)
    add_subdirectory(tests)
endif (BUILD_TESTING AND LIBJITTER_BUILD_TESTS)

if (BUILD_BENCHMARK AND LIBJITTER_BUILD_BENCHMARK)
    set(BENCHMARK_USE_BUNDLED_GTEST OFF)
    set(BENCHMARK_ENABLE_TESTING OFF)
    add_subdirectory(dependencies/benchmark)
    add_subdirectory(benchmark)
endif (BUILD_BENCHMARK AND LIBJITTER_BUILD_BENCHMARK)