# 拆分算法/渲染为两个可独立编译项目 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (or subagent-driven-development) to implement this plan task-by-task. Steps use checkbox (`- [ ]`).

**Goal:** 把 OcctRenderLab 拆成 `contour`(纯 OCCT 算法库,可独立编译/安装/find_package)+ `render`(VSG 渲染,依赖 contour+vsg+vsgocct),同一仓库、超级构建。

**Architecture:** 根 CMake 做 super-build(`add_subdirectory(contour)` [+ `render`]);`contour` 自身是完整可独立 `project()`,导出 `occtcontour::occtcontour` 并提供 install/Config;`render` 在超级构建时用同树目标、独立时 `find_package(occtcontour)`。`ModelLoad` 改用 OCCT `STEPControl_Reader`,算法库零 vsg。

**Tech Stack:** CMake ≥3.21,MSVC(VS 18 2026),OCCT 8.0(D:/OCCT/build),vsg 1.1.13,vsgocct。

---

## 已核实 API
- `STEPControl_Reader::ReadStream(const char* theName, std::istream&)`(STEPControl_Reader.hxx:91);`TransferRoots()`(XSControl_Reader.hxx:198);`NbShapes()`(:206);`Shape(int num=1)`(:212)。`ReadStream` 的 name 须绑定到具名变量再 `.c_str()`(避免临时串悬垂)。

## 文件迁移映射(git mv,保留历史)
```
src/OuterContour.h          → contour/include/occtcontour/OuterContour.h
src/OuterContour.cpp        → contour/src/OuterContour.cpp
src/ModelLoad.h             → contour/include/occtcontour/ModelLoad.h
src/ModelLoad.cpp           → contour/src/ModelLoad.cpp        (改 OCCT 读取)
tests/contour_tests.cpp     → contour/tests/contour_tests.cpp
tests/data/*                → contour/tests/data/*
apps/contour_sweep/         → contour/apps/contour_sweep/
apps/contour_svg/           → contour/apps/contour_svg/
apps/contour_viewer/OutlineRender.{h,cpp} → render/src/OutlineRender.cpp + render/include/ocrlrender/OutlineRender.h
apps/contour_viewer/main.cpp              → render/apps/contour_viewer/main.cpp
apps/contour_viewer/selfcheck.cpp         → render/apps/contour_viewer/selfcheck.cpp
(删除旧 src/, tests/, apps/ 顶层 CMakeLists;根 CMakeLists 重写)
```
include 路径统一改:`#include "OuterContour.h"`/`"ModelLoad.h"` → `#include <occtcontour/OuterContour.h>`/`<occtcontour/ModelLoad.h>`;render 内 `#include "OutlineRender.h"` → `#include <ocrlrender/OutlineRender.h>`。C++ 命名空间 `ocrl` 不变。

---

## Task 1: 迁移算法文件到 contour/ 并修 include 路径

**Files:** git mv 如上(算法侧);改算法文件/测试/工具的 include 路径。

- [ ] **Step 1: 建目录 + git mv 算法文件**
```powershell
cd D:/OcctRenderLab
New-Item -ItemType Directory -Force contour/include/occtcontour, contour/src, contour/tests/data, contour/apps, contour/cmake | Out-Null
git mv src/OuterContour.h   contour/include/occtcontour/OuterContour.h
git mv src/ModelLoad.h      contour/include/occtcontour/ModelLoad.h
git mv src/OuterContour.cpp contour/src/OuterContour.cpp
git mv src/ModelLoad.cpp    contour/src/ModelLoad.cpp
git mv tests/contour_tests.cpp contour/tests/contour_tests.cpp
git mv tests/data contour/tests/data
git mv apps/contour_sweep contour/apps/contour_sweep
git mv apps/contour_svg   contour/apps/contour_svg
```

- [ ] **Step 2: 修 include 路径**
- `contour/src/OuterContour.cpp`:首行 `#include "OuterContour.h"` → `#include <occtcontour/OuterContour.h>`
- `contour/src/ModelLoad.cpp`:`#include "ModelLoad.h"` → `#include <occtcontour/ModelLoad.h>`(本任务先只改路径;OCCT 读取在 Task 3)
- `contour/tests/contour_tests.cpp`:`#include "ModelLoad.h"`/`"OuterContour.h"` → `<occtcontour/ModelLoad.h>`/`<occtcontour/OuterContour.h>`
- `contour/apps/contour_sweep/main.cpp` 与 `contour/apps/contour_svg/main.cpp`:同上两个头改成 `<occtcontour/...>`

- [ ] **Step 3: 提交(暂不构建,CMake 在 Task 2/5)**
```powershell
git add -A; git commit -m "refactor: 算法文件迁入 contour/ 并改 include 为 <occtcontour/...>"
```

---

## Task 2: contour/ 独立 CMake(库 + apps + tests + install/export)

**Files:** Create `contour/CMakeLists.txt`, `contour/cmake/occtcontourConfig.cmake.in`

- [ ] **Step 1: 写 `contour/CMakeLists.txt`**
```cmake
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
```

- [ ] **Step 2: 写 `contour/cmake/occtcontourConfig.cmake.in`**
```cmake
@PACKAGE_INIT@
# occtcontour 依赖 OCCT(本机构建树 targets)。消费方无需自己 find OCCT。
if(NOT TARGET TKHLR)
    include("D:/OCCT/build/OpenCASCADETargets.cmake")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/occtcontourTargets.cmake")
check_required_components(occtcontour)
```

---

## Task 3: ModelLoad 改用 OCCT STEPControl_Reader(去 vsgocct)

**Files:** Modify `contour/src/ModelLoad.cpp`

- [ ] **Step 1: 整体替换 `contour/src/ModelLoad.cpp`**
```cpp
#include <occtcontour/ModelLoad.h>

#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Shape.hxx>

#include <fstream>
#include <stdexcept>
#include <string>

namespace ocrl {
TopoDS_Compound loadCompound(const std::filesystem::path& stepFile)
{
    std::ifstream input(stepFile, std::ios::binary);  // ifstream 兼容中文/Unicode 路径
    if (!input)
        throw std::runtime_error("cannot open STEP: " + stepFile.u8string());

    STEPControl_Reader reader;
    const std::string name = stepFile.filename().u8string();   // 绑定到具名变量,避免 c_str() 悬垂
    if (reader.ReadStream(name.c_str(), input) != IFSelect_RetDone)
        throw std::runtime_error("OCCT failed to read STEP: " + stepFile.u8string());
    reader.TransferRoots();

    TopoDS_Compound out;
    BRep_Builder builder;
    builder.MakeCompound(out);
    for (int i = 1; i <= reader.NbShapes(); ++i) {
        const TopoDS_Shape s = reader.Shape(i);
        if (!s.IsNull()) builder.Add(out, s);
    }
    return out;
}
} // namespace ocrl
```
(`ModelLoad.h` 内容不变,仅路径已迁移。)

- [ ] **Step 2: 提交(配合 Task 2)**
```powershell
git add -A; git commit -m "feat(contour): 独立 CMake + install/export;ModelLoad 改用 OCCT STEPControl_Reader(去 vsgocct)"
```

---

## Task 4: 迁移渲染文件到 render/ 并写 render/CMakeLists.txt

**Files:** git mv 渲染文件;Create `render/CMakeLists.txt`;改 include 路径。

- [ ] **Step 1: 建目录 + git mv**
```powershell
New-Item -ItemType Directory -Force render/include/ocrlrender, render/src, render/apps/contour_viewer | Out-Null
git mv apps/contour_viewer/OutlineRender.h   render/include/ocrlrender/OutlineRender.h
git mv apps/contour_viewer/OutlineRender.cpp render/src/OutlineRender.cpp
git mv apps/contour_viewer/main.cpp          render/apps/contour_viewer/main.cpp
git mv apps/contour_viewer/selfcheck.cpp     render/apps/contour_viewer/selfcheck.cpp
git rm apps/contour_viewer/CMakeLists.txt tests/CMakeLists.txt
```

- [ ] **Step 2: 修 render include 路径**
- `render/src/OutlineRender.cpp`:`#include "OutlineRender.h"` → `#include <ocrlrender/OutlineRender.h>`;`#include "OuterContour.h"`(若有)→ `<occtcontour/OuterContour.h>`
- `render/include/ocrlrender/OutlineRender.h`:`#include "OuterContour.h"` → `<occtcontour/OuterContour.h>`
- `render/apps/contour_viewer/main.cpp`:`#include "OutlineRender.h"`→`<ocrlrender/OutlineRender.h>`;`"ModelLoad.h"`→`<occtcontour/ModelLoad.h>`;`"OuterContour.h"`→`<occtcontour/OuterContour.h>`
- `render/apps/contour_viewer/selfcheck.cpp`:`"OutlineRender.h"`→`<ocrlrender/OutlineRender.h>`

- [ ] **Step 3: 写 `render/CMakeLists.txt`**
```cmake
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
```

---

## Task 5: 根 CMakeLists 改为 super-build

**Files:** Overwrite `CMakeLists.txt`(根)

- [ ] **Step 1: 整体替换根 `CMakeLists.txt`**
```cmake
cmake_minimum_required(VERSION 3.21)
project(OcctRenderLab CXX)

# OCCT/Vulkan 只有 Debug 产物 —— 全局单一 Debug 配置(子项目也继承)
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug" CACHE STRING "" FORCE)
endif()

option(OCRL_BUILD_RENDER "Build the VSG render project (needs vsg/vsgocct/Qt)" ON)

add_subdirectory(contour)
if(OCRL_BUILD_RENDER)
    add_subdirectory(render)
endif()
```

- [ ] **Step 2: 提交**
```powershell
git add -A; git commit -m "refactor: render 迁入 render/;根 CMake 改 super-build(-DOCRL_BUILD_RENDER 控制)"
```

---

## Task 6: 超级构建 + ctest 回归(全绿)

- [ ] **Step 1: 干净配置 + 全量构建**
```powershell
Remove-Item -Recurse -Force D:/OcctRenderLab/build -ErrorAction SilentlyContinue
cmake -S D:/OcctRenderLab -B D:/OcctRenderLab/build -G "Visual Studio 18 2026" -A x64
cmake --build D:/OcctRenderLab/build --config Debug
```
Expected: 0 错误;产出 `occtcontour.lib`、`contour_tests/sweep/svg.exe`、`ocrl_render.lib`、`contour_viewer.exe`。
(子目录路径:`build/contour/...`、`build/render/...`。)

- [ ] **Step 2: ctest + viewer 冒烟**
```powershell
ctest --test-dir D:/OcctRenderLab/build -C Debug --output-on-failure
D:/OcctRenderLab/build/render/Debug/contour_viewer.exe D:/OcctRenderLab/contour/tests/data/knife.step --frames 5
```
Expected: `100% tests passed`(cylinder=4π、knife 外环 198 等不变);viewer 打印 `loops=10` 并 `rendered 5 frames`。
(注意 exe 路径以实际生成为准:VS 生成器下通常是 `build/contour/Debug/contour_tests.exe`、`build/render/apps/contour_viewer/Debug/contour_viewer.exe`;用 `Get-ChildItem -Recurse -Filter *.exe` 确认。)

- [ ] **Step 3: 提交(若有 CMake 微调)**
```powershell
git add -A; git commit -m "build: 超级构建打通,ctest 全过,viewer 冒烟通过"
```

---

## Task 7: 验证「只编算法」+ 算法库零 vsg

- [ ] **Step 1: 只编算法配置 + 构建**
```powershell
cmake -S D:/OcctRenderLab -B D:/OcctRenderLab/build_algo -G "Visual Studio 18 2026" -A x64 -DOCRL_BUILD_RENDER=OFF
cmake --build D:/OcctRenderLab/build_algo --config Debug
```
Expected: 配置日志中**无 vsg/Qt 查找**;构建出 `occtcontour` + contour_tests/sweep/svg,无 render 目标。

- [ ] **Step 2: 确认 occtcontour 不链 vsg**
```powershell
# contour_tests/sweep 旁不应出现 vsg.dll(只有 OCCT 的 TK*.dll)
Get-ChildItem D:/OcctRenderLab/build_algo -Recurse -Filter "vsg*.dll"
```
Expected: 无输出(算法侧零 vsg)。

- [ ] **Step 3: ctest(only-algo)**
```powershell
ctest --test-dir D:/OcctRenderLab/build_algo -C Debug
```
Expected: `100% tests passed`。

---

## Task 8: find_package 消费示例 + 验证

**Files:** Create `contour/examples/consumer_smoke/CMakeLists.txt`, `.../main.cpp`

- [ ] **Step 1: 写最小外部消费工程**
`contour/examples/consumer_smoke/main.cpp`:
```cpp
#include <occtcontour/ModelLoad.h>
#include <occtcontour/OuterContour.h>
#include <cstdio>
int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: consumer_smoke <model.step>\n"); return 1; }
    TopoDS_Compound c = ocrl::loadCompound(argv[1]);
    ocrl::ContourResult r = ocrl::computeOuterContour(c, {});
    std::printf("consumer ok=%d loops=%zu area=%.3f\n", r.ok, r.loopCount, r.area);
    return r.ok ? 0 : 1;
}
```
`contour/examples/consumer_smoke/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.21)
project(consumer_smoke CXX)
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug" CACHE STRING "" FORCE)
endif()
find_package(occtcontour REQUIRED)
add_executable(consumer_smoke main.cpp)
target_link_libraries(consumer_smoke PRIVATE occtcontour::occtcontour)
if(WIN32)
    add_custom_command(TARGET consumer_smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t "$<TARGET_FILE_DIR:consumer_smoke>" $<TARGET_RUNTIME_DLLS:consumer_smoke>
        COMMAND_EXPAND_LISTS)
endif()
```

- [ ] **Step 2: install 算法库到本地 prefix,再用 find_package 编消费工程**
```powershell
cmake --install D:/OcctRenderLab/build_algo --config Debug --prefix D:/OcctRenderLab/_install
cmake -S D:/OcctRenderLab/contour/examples/consumer_smoke -B D:/OcctRenderLab/_consumer -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=D:/OcctRenderLab/_install
cmake --build D:/OcctRenderLab/_consumer --config Debug
D:/OcctRenderLab/_consumer/Debug/consumer_smoke.exe D:/OcctRenderLab/contour/tests/data/cylinder.step
```
Expected: `find_package(occtcontour)` 成功;`consumer ok=1 loops=1 area=12.566`。
(把 `_install`、`_consumer`、`build_algo` 加入 `.gitignore`。)

- [ ] **Step 3: 提交**
```powershell
git add -A; git commit -m "test: find_package 消费示例 consumer_smoke + 只编算法验证"
```

---

## Task 9: 更新 README + 推送

**Files:** Modify `README.md`、`.gitignore`

- [ ] **Step 1: README 增「项目结构」「单独使用算法」两节**
说明:`contour/`(纯 OCCT 算法,`find_package(occtcontour)` 或 `add_subdirectory(contour)`)与 `render/`(VSG);
超级构建命令;`-DOCRL_BUILD_RENDER=OFF` 只编算法;消费示例。更新各 exe 的新路径。
`.gitignore` 追加 `/_install/`、`/_consumer/`、`/build_algo/`。

- [ ] **Step 2: 合并回 master 并推送**
```powershell
ctest --test-dir D:/OcctRenderLab/build -C Debug
git add -A; git commit -m "docs: README 项目结构与单独使用算法说明"
git checkout master; git merge --ff-only refactor/split-algo-render
git push origin master
```

---

## Self-Review

**Spec 覆盖**:§3 纯OCCT→Task3;§4 结构→Task1/4/5;§7 CMake(含 install/export+双消费)→Task2/4/5/8;§6 ModelLoad→Task3;§8 验证(超级构建/只编算法/零vsg/find_package/渲染回归)→Task6/7/8 + Task6 viewer 冒烟。全覆盖。
**占位符**:无 TBD;exe 路径处注明"以实际生成为准 + Get-ChildItem 确认"(VS 生成器子目录布局可能为 `build/contour/Debug/` 或 `build/contour/apps/.../Debug/`),非占位,是必要的运行期核对。
**类型一致**:库目标统一 `occtcontour`/别名 `occtcontour::occtcontour`/包名 `occtcontour`;头路径统一 `<occtcontour/...>`、`<ocrlrender/...>`;命名空间 `ocrl` 不变;`loadCompound`/`computeOuterContour`/`ContourResult` 签名不变。
