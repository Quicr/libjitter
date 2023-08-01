add_executable(libjitter_benchmark benchmark.cpp)
target_link_libraries(libjitter_benchmark PRIVATE libjitter benchmark::benchmark_main)
set_target_properties(libjitter_benchmark PROPERTIES
                      CXX_STANDARD 17)