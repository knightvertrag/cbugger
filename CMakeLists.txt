cmake_minimum_required(VERSION 3.19)
project(cbugger VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(PkgConfig REQUIRED)
pkg_check_modules(libedit REQUIRED IMPORTED_TARGET libedit)

find_package(spdlog CONFIG REQUIRED)

add_subdirectory("src")
add_subdirectory("tools")

if (BUILD_TESTING)
    find_package(Catch2 CONFIG REQUIRED)
    add_subdirectory("test") 
endif()