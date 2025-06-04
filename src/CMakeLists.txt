add_library(libcbg STATIC libcbg.cpp)
add_library(cbg::libcbg ALIAS libcbg)

set_target_properties(
    libcbg
    PROPERTIES OUTPUT_NAME cbg
)
target_compile_features(libcbg PUBLIC cxx_std_17)

target_include_directories(libcbg
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
)