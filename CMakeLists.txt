cmake_minimum_required(VERSION 3.0.0)
project(cbugger VERSION 0.1.0)

include(CTest)
enable_testing()

include_directories(dependencies/libelfin dependencies/linenoise include)
add_executable(cbugger src/main.cpp src/debugger.cpp dependencies/linenoise/linenoise.c)

add_custom_target(
   libelfin
   COMMAND make
   WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/dependencies/libelfin
)
target_link_libraries(cbugger
                      ${PROJECT_SOURCE_DIR}/dependencies/libelfin/dwarf/libdwarf++.so
                      ${PROJECT_SOURCE_DIR}/dependencies/libelfin/elf/libelf++.so)
add_dependencies(cbugger libelfin)
set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)  
