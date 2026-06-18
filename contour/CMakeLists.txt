cmake_minimum_required(VERSION 3.21)
project(occtcontour CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(WIN32)
    add_definitions(-DNOMINMAX)
endif()
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug" CACHE STRING "" FORCE)
endif()
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE        Debug)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL     Debug)

# OCCT(构建树 targets;避免被上层重复 include)
set(OCCT_BUILD_DIR "D:/OCCT/build" CACHE PATH "OCCT build tree")
if(NOT TARGET TKHLR)
    include("${OCCT_BUILD_DIR}/OpenCASCADETargets.cmake")
endif()
set(OpenCASCADE_INCLUDE_DIR "${OCCT_BUILD_DIR}/inc")

set(OCRL_OCCT_LIBS
    TKHLR TKShHealing TKMesh TKBO TKDESTEP
    TKTopAlgo TKBRep TKGeomAlgo TKGeomBase TKG3d TKG2d TKMath TKernel)

if(NOT COMMAND ocrl_copy_runtime_dlls)
    function(ocrl_copy_runtime_dlls target_name)
        if(WIN32)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy -t "$<TARGET_FILE_DIR:${target_name}>"
                        $<TARGET_RUNTIME_DLLS:${target_name}>
                COMMAND_EXPAND_LISTS)
        endif()
    endfunction()
endif()

add_library(occtcontour STATIC src/OuterContour.cpp src/ModelLoad.cpp)
add_library(occtcontour::occtcontour ALIAS occtcontour)
target_compile_features(occtcontour PUBLIC cxx_std_17)
target_compile_options(occtcontour PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_include_directories(occtcontour PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(occtcontour PUBLIC ${OCRL_OCCT_LIBS})

option(OCCTCONTOUR_BUILD_APPS  "Build pure-OCCT tools" ON)
option(OCCTCONTOUR_BUILD_TESTS "Build tests" ON)

if(OCCTCONTOUR_BUILD_APPS)
    add_executable(contour_sweep apps/contour_sweep/main.cpp)
    target_link_libraries(contour_sweep PRIVATE occtcontour::occtcontour)
    ocrl_copy_runtime_dlls(contour_sweep)

    add_executable(contour_svg apps/contour_svg/main.cpp)
    target_link_libraries(contour_svg PRIVATE occtcontour::occtcontour)
    ocrl_copy_runtime_dlls(contour_svg)
endif()

if(OCCTCONTOUR_BUILD_TESTS)
    enable_testing()
    add_executable(contour_tests tests/contour_tests.cpp)
    target_link_libraries(contour_tests PRIVATE occtcontour::occtcontour)
    target_compile_definitions(contour_tests PRIVATE
        TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    ocrl_copy_runtime_dlls(contour_tests)
    add_test(NAME contour_tests COMMAND contour_tests)
endif()

# ---- 安装 / 导出(供 find_package(occtcontour)) ----
include(CMakePackageConfigHelpers)
install(TARGETS occtcontour EXPORT occtcontourTargets
        ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT occtcontourTargets
        NAMESPACE occtcontour:: DESTINATION lib/cmake/occtcontour
        FILE occtcontourTargets.cmake)
configure_package_config_file(cmake/occtcontourConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/occtcontourConfig.cmake"
    INSTALL_DESTINATION lib/cmake/occtcontour)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/occtcontourConfig.cmake"
        DESTINATION lib/cmake/occtcontour)
