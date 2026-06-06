add_executable(tests
    test_process.cpp
    test_registers.cpp
    test_breakpoints.cpp
)
add_subdirectory(targets)

# Ensure helper programs are built when the test executable is built.
# These are spawned by the Catch2 tests using relative paths (e.g. "targets/run_endlessly").
add_dependencies(tests
    run_endlessly
    end_immediately
    register_write
    register_read
)

target_link_libraries(tests PRIVATE cbg::libcbg Catch2::Catch2WithMain)

# Integrate with CTest: discover individual TEST_CASEs so ctest can run them.
include(Catch)
catch_discover_tests(tests)