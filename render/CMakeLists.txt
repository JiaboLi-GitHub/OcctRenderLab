cmake_minimum_required(VERSION 3.21)
project(ocrl_render CXX)

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

# 独立编译 render 时才需要找 occtcontour;超级构建时该目标已存在
if(NOT TARGET occtcontour::occtcontour)
    find_package(occtcontour REQUIRED)
endif()

# vsg + 复用 vsgocct
find_package(vsg 1.1.2 REQUIRED HINTS "C:/Program Files (x86)/vsg/lib/cmake/vsg")
if(NOT TARGET vsgocct::vsgocct)
    add_subdirectory("D:/vsgOcct/src" "${CMAKE_BINARY_DIR}/_vsgocct")
endif()

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

add_library(ocrl_render STATIC src/OutlineRender.cpp)
target_include_directories(ocrl_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ocrl_render PUBLIC occtcontour::occtcontour vsg::vsg)

add_executable(outline_render_selfcheck apps/contour_viewer/selfcheck.cpp)
target_link_libraries(outline_render_selfcheck PRIVATE ocrl_render)
ocrl_copy_runtime_dlls(outline_render_selfcheck)

add_executable(contour_viewer apps/contour_viewer/main.cpp)
target_link_libraries(contour_viewer PRIVATE ocrl_render vsgocct::vsgocct)
ocrl_copy_runtime_dlls(contour_viewer)
