add_executable(tests tests.cpp)
add_subdirectory(targets)
target_link_libraries(tests PRIVATE cbg::libcbg Catch2::Catch2WithMain)