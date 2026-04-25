if(LEDGERFLOW_BUILD_TESTS)
    add_subdirectory(unit)
endif()

if(LEDGERFLOW_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()