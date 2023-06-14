cmake_minimum_required(VERSION 3.14)

project(libjitter)

add_library(libjitter JitterBuffer.cpp include/JitterBuffer.hh include/Packet.h)
target_include_directories(libjitter PUBLIC include)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 11)

add_library(clibjitter SHARED libjitter.cpp include/libjitter.h include/Packet.h)
target_include_directories(clibjitter PUBLIC include)
target_link_libraries(clibjitter PUBLIC libjitter)
set_property(TARGET clibjitter PROPERTY CXX_STANDARD 11)

if(BUILD_TESTING AND LIBJITTER_BUILD_TESTS)
    add_subdirectory(tests)
endif(BUILD_TESTING AND LIBJITTER_BUILD_TESTS)
