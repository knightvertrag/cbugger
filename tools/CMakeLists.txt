add_executable(cbg cbg.cpp)
target_link_libraries(cbg PRIVATE cbg::libcbg PkgConfig::libedit spdlog::spdlog)