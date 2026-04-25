cmake_minimum_required(VERSION 3.15)
project(LedgerFlow)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE LEDGERFLOW_SOURCES CONFIGURE_DEPENDS src/**/*.cpp)

add_library(ledgerflow_lib STATIC ${LEDGERFLOW_SOURCES})
target_include_directories(ledgerflow_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(LedgerFlow app/main.cpp
        include/ledgerflow/core/ring_buffer.hpp)
target_link_libraries(LedgerFlow PRIVATE ledgerflow_lib)

option(LEDGERFLOW_BUILD_TESTS "Build unit tests" OFF)
option(LEDGERFLOW_BUILD_BENCHMARKS "Build benchmarks" OFF)

if(LEDGERFLOW_BUILD_TESTS)
    enable_testing()
endif()

if(LEDGERFLOW_BUILD_TESTS OR LEDGERFLOW_BUILD_BENCHMARKS)
    add_subdirectory(test)
endif()