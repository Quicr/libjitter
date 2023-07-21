project(libjitter_test)
add_executable(${PROJECT_NAME}
               main.cpp
               implementation_test.cpp
               api_test.cpp
               test_functions.h
               BufferInspector.cpp
               BufferInspector.hh)
target_link_libraries(${PROJECT_NAME} PRIVATE doctest::doctest libjitter)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -Werror)
include(${CMAKE_SOURCE_DIR}/dependencies/doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(${PROJECT_NAME})