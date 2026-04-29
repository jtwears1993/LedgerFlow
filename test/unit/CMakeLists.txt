include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(ledgerflow_unit_tests
    wal_constructor_test.cpp
    wal_append_test.cpp
    wal_commit_test.cpp
    wal_recover_test.cpp
    wal_io_test.cpp
        ../../include/ledgerflow/protocol.hpp
)

target_link_libraries(ledgerflow_unit_tests
    PRIVATE
    ledgerflow_lib
    GTest::gtest_main
)

target_include_directories(ledgerflow_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(GoogleTest)
gtest_discover_tests(ledgerflow_unit_tests)