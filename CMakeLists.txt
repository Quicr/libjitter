cmake_minimum_required(VERSION 3.14)

project(libjitter)
include(CTest)

add_library(libjitter JitterBuffer.cpp include/JitterBuffer.hh include/Packet.h)
target_include_directories(libjitter PUBLIC include)
set_property(TARGET libjitter PROPERTY CXX_STANDARD 17)
set_target_properties(libjitter PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(clibjitter SHARED libjitter.cpp include/libjitter.h)
target_include_directories(clibjitter PUBLIC include)
target_link_libraries(clibjitter PUBLIC libjitter)
set_property(TARGET clibjitter PROPERTY CXX_STANDARD 17)

if (BUILD_TESTING AND LIBJITTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/doctest)
    add_compile_definitions(LIBJITTER_BUILD_TESTS)
    add_subdirectory(tests)
endif (BUILD_TESTING AND LIBJITTER_BUILD_TESTS)
