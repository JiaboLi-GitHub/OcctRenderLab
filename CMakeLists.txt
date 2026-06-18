cmake_minimum_required(VERSION 3.21)
project(OcctRenderLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(WIN32)
    add_definitions(-DNOMINMAX)
endif()

# OCCT 与 Vulkan 只构建/提供了 Debug 产物;VS 多配置生成器默认要 4 个配置,
# 会因 Release/RelWithDebInfo/MinSizeRel 找不到 IMPORTED_LOCATION 而失败。
# 与 vsgOcct 一致:强制单一 Debug 配置。
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug" CACHE STRING "" FORCE)
endif()

# OCCT 仅构建了 Debug:把任意配置映射到 Debug 导入产物
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE        Debug)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL     Debug)

# --- OCCT(构建树):include 自包含 targets 文件,定义全部 TK* 导入目标 ---
set(OCCT_BUILD_DIR "D:/OCCT/build" CACHE PATH "OCCT build tree")
include("${OCCT_BUILD_DIR}/OpenCASCADETargets.cmake")
set(OpenCASCADE_INCLUDE_DIR "${OCCT_BUILD_DIR}/inc")

# --- VSG ---
find_package(vsg 1.1.2 REQUIRED
    HINTS "C:/Program Files (x86)/vsg/lib/cmake/vsg")

# --- 复用 vsgocct 静态库(只取 src 子树,避开 vsgQt/examples/tests 副作用) ---
add_subdirectory("D:/vsgOcct/src" "${CMAKE_BINARY_DIR}/_vsgocct")

# OCCT HLR/外轮廓所需工具包(TKHLR 传递大多数基础库,TKShHealing/TKMesh 需显式)
set(OCRL_OCCT_LIBS
    TKHLR TKShHealing TKMesh
    TKTopAlgo TKBRep TKGeomAlgo TKGeomBase TKG3d TKG2d TKMath TKernel)

# 把运行期 DLL(OCCT + vsg)拷到目标 exe 旁
function(ocrl_copy_runtime_dlls target_name)
    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy -t "$<TARGET_FILE_DIR:${target_name}>"
                    $<TARGET_RUNTIME_DLLS:${target_name}>
            COMMAND_EXPAND_LISTS)
    endif()
endfunction()

# 核心库:ModelLoad + OuterContour
add_library(ocrl_core STATIC
    src/ModelLoad.cpp
    src/OuterContour.cpp)
target_include_directories(ocrl_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${OpenCASCADE_INCLUDE_DIR}")
target_link_libraries(ocrl_core PUBLIC
    vsgocct::vsgocct          # 传播 vsg::vsg + vsgocct include 根
    ${OCRL_OCCT_LIBS})
# UTF-8 源码/字符串(中文消息、避免 MSVC C4819);传播到 tests/apps
target_compile_options(ocrl_core PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

enable_testing()
add_subdirectory(tests)
add_subdirectory(apps/contour_viewer)
add_subdirectory(apps/contour_sweep)
