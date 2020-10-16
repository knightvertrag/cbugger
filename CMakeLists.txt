cmake_minimum_required(VERSION 3.0.0)
project(debugger VERSION 0.1.0)

include(CTest)
enable_testing()

include_directories(dependencies/libelfin dependencies/linenoise include)
add_executable(debugger src/main.cpp dependencies/linenoise/linenoise.c)

add_custom_target(
   libelfin
   COMMAND ${MAKE}
   WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/dependencies/libelfin
)
target_link_libraries(debugger
                      ${PROJECT_SOURCE_DIR}/dependencies/libelfin/dwarf/libdwarf++.so
                      ${PROJECT_SOURCE_DIR}/dependencies/libelfin/elf/libelf++.so)
add_dependencies(debugger libelfin)
set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)  
