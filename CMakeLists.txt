cmake_minimum_required(VERSION 3.21)
project(OcctRenderLab CXX)

# OCCT/Vulkan 只提供 Debug 产物 —— 全局单一 Debug 配置(子项目继承)
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug" CACHE STRING "" FORCE)
endif()

# 算法(纯 OCCT)总是构建;渲染(vsg/vsgocct)可关
option(OCRL_BUILD_RENDER "Build the VSG render project (needs vsg/vsgocct)" ON)

enable_testing()   # 让 ctest 在根 build 目录聚合 contour/ 里注册的测试

add_subdirectory(contour)
if(OCRL_BUILD_RENDER)
    add_subdirectory(render)
endif()
