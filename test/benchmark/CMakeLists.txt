include(FetchContent)

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.8.4
)
set(BENCHMARK_ENABLE_TESTING      OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

add_executable(ledgerflow_benchmarks wal_benchmark.cpp)

target_link_libraries(ledgerflow_benchmarks
    PRIVATE
    ledgerflow_lib
    benchmark::benchmark_main
)
