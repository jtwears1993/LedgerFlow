cmake_minimum_required(VERSION 3.15)
project(LedgerFlow)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(ExternalProject)

find_package(Protobuf REQUIRED)

set(LIBURING_INSTALL_DIR ${CMAKE_BINARY_DIR}/_deps/liburing-install)
set(LIBURING_LIBRARY ${LIBURING_INSTALL_DIR}/lib/liburing.a)
file(MAKE_DIRECTORY ${LIBURING_INSTALL_DIR}/include)

ExternalProject_Add(liburing_external
        GIT_REPOSITORY https://github.com/axboe/liburing.git
        GIT_TAG liburing-2.8
        PREFIX ${CMAKE_BINARY_DIR}/_deps/liburing
        BUILD_IN_SOURCE TRUE
        CONFIGURE_COMMAND <SOURCE_DIR>/configure
                --prefix=${LIBURING_INSTALL_DIR}
                --cc=${CMAKE_C_COMPILER}
                --cxx=${CMAKE_CXX_COMPILER}
        BUILD_COMMAND make -j
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${LIBURING_LIBRARY}
)

add_library(liburing::uring STATIC IMPORTED GLOBAL)
add_dependencies(liburing::uring liburing_external)
set_target_properties(liburing::uring PROPERTIES
        IMPORTED_LOCATION ${LIBURING_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${LIBURING_INSTALL_DIR}/include
)

set(LEDGERFLOW_PROTO_VERSION v1)
set(LEDGERFLOW_GENERATED_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated/proto/${LEDGERFLOW_PROTO_VERSION}/cpp)

add_library(ledgerflow_proto STATIC
        ${LEDGERFLOW_GENERATED_PROTO_DIR}/ingress.pb.cc
        ${LEDGERFLOW_GENERATED_PROTO_DIR}/ingress.pb.h
)
target_include_directories(ledgerflow_proto PUBLIC ${LEDGERFLOW_GENERATED_PROTO_DIR})
target_link_libraries(ledgerflow_proto PUBLIC protobuf::libprotobuf)

file(GLOB_RECURSE LEDGERFLOW_SOURCES CONFIGURE_DEPENDS src/**/*.cpp)

add_library(ledgerflow_lib STATIC ${LEDGERFLOW_SOURCES}
        include/ledgerflow/core/enums.hpp
        include/ledgerflow/event_parser.hpp
)
target_include_directories(ledgerflow_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ledgerflow_lib PUBLIC ledgerflow_proto liburing::uring)

add_executable(LedgerFlow app/main.cpp
        include/ledgerflow/core/ring_buffer.hpp
        include/ledgerflow/core/enums.hpp
        include/ledgerflow/core/events.hpp
        include/ledgerflow/core/event_mapper.hpp
        include/ledgerflow/server.hpp
        include/ledgerflow/core/calculations.hpp
        include/ledgerflow/event_parser.hpp
        include/ledgerflow/config.hpp
        include/ledgerflow/shutdown_manager.hpp
)
target_link_libraries(LedgerFlow PRIVATE ledgerflow_lib)

add_executable(ledgerflow_proto_smoke
        test/proto_smoke.cpp
)
target_link_libraries(ledgerflow_proto_smoke PRIVATE ledgerflow_lib)

option(LEDGERFLOW_BUILD_TESTS "Build unit tests" OFF)
option(LEDGERFLOW_BUILD_BENCHMARKS "Build benchmarks" OFF)

if(LEDGERFLOW_BUILD_TESTS)
    enable_testing()
endif()

if(LEDGERFLOW_BUILD_TESTS OR LEDGERFLOW_BUILD_BENCHMARKS)
    add_subdirectory(test)
endif()
