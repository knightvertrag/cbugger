cmake_minimum_required(VERSION 3.19)
project(cbugger VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include_directories(${CMAKE_SOURCE_DIR}/include)

add_subdirectory("src")
add_subdirectory("tools")

# if (BUILD_TESTING)
#    add_subdirectory("tests") 
# endif()