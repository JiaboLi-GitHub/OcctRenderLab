# 设计文档:拆分「算法」与「渲染」为两个可独立编译的项目(同一仓库)

- 日期:2026-06-18
- 仓库:`OcctRenderLab`(GitHub: JiaboLi-GitHub/OcctRenderLab)
- 状态:待用户复核 → writing-plans

## 1. 目标与动机

把当前单体仓库拆成**两个可独立编译的项目**(仍在同一个 git 仓库):

- **算法项目 `contour`**:模型外轮廓计算,**只依赖 OCCT**,可独立编译、安装、被其他项目
  通过 `find_package` 或 `add_subdirectory` 复用。
- **渲染项目 `render`**:VSG 可视化 demo,依赖 `contour` + vsg + vsgocct。

动机:用户的**其他项目需要单独使用外轮廓算法**,不应被迫连同整个 vsg/vsgocct 一起编译。

## 2. 现状与核心问题

当前 `ocrl_core`(`ModelLoad` + `OuterContour`)虽是"算法核心",但 `target_link_libraries`
里 PUBLIC 链了 `vsgocct::vsgocct`,而 vsgocct 又 PUBLIC 链 `vsg::vsg` —— **算法核心实际拖着
整个 vsg**。根因是 `ModelLoad.cpp` 用了 `vsgocct::cad::readStep` 读 STEP。

要让算法"单独可用",必须切断这条依赖。

## 3. 关键决策(已与用户确认)

1. **算法库依赖边界 = 纯 OCCT**。`ModelLoad` 改用 OCCT 自带的 `STEPControl_Reader` 读 STEP,
   不再依赖 vsgocct。算法库零 vsg/vsgocct/Qt 依赖。
2. **消费方式 = 两者都支持**:`install(EXPORT)` + 包配置(`find_package(occtcontour)`)
   **和** `add_subdirectory(.../contour)` 源码引入都可用。
3. 渲染侧仍用 vsgocct 建实体场景(`buildAssemblyScene`),不变。

## 4. 目标目录结构

```
OcctRenderLab/                         ← 仓库根 = 超级构建(super-build)
├─ CMakeLists.txt                      add_subdirectory(contour); 若 OCRL_BUILD_RENDER(默认 ON) 再 add_subdirectory(render)
├─ README.md / docs/ / .gitignore / .gitattributes
│
├─ contour/                            ← ① 算法项目(纯 OCCT)
│  ├─ CMakeLists.txt                   独立 project(occtcontour);库 + tests + apps + install/export
│  ├─ include/occtcontour/
│  │   ├─ OuterContour.h
│  │   └─ ModelLoad.h
│  ├─ src/
│  │   ├─ OuterContour.cpp
│  │   └─ ModelLoad.cpp                ← 改用 STEPControl_Reader(ifstream+ReadStream,兼容中文路径)
│  ├─ apps/
│  │   ├─ contour_sweep/main.cpp       纯 OCCT 工具(示范单独使用)
│  │   └─ contour_svg/main.cpp         纯 OCCT 工具(导出 SVG / 取证)
│  ├─ tests/
│  │   ├─ contour_tests.cpp            解析验证(纯 OCCT)
│  │   └─ data/{cylinder,sphere,plate,bar,knife}.step
│  └─ cmake/occtcontourConfig.cmake.in 包配置模板(被 find_package 用,内部再 include OCCT targets)
│
└─ render/                             ← ② 渲染项目(依赖 ① + vsg + vsgocct)
   ├─ CMakeLists.txt                   独立 project(ocrl_render);超级构建时直接用 occtcontour::occtcontour 目标,
   │                                    独立编译时 find_package(occtcontour)
   ├─ include/ocrlrender/OutlineRender.h
   ├─ src/OutlineRender.cpp
   └─ apps/contour_viewer/main.cpp
```

命名:库目标 `occtcontour`,命名空间别名 `occtcontour::occtcontour`,find_package 包名 `occtcontour`;
C++ 命名空间沿用 `ocrl`(减少改动)。渲染库 `ocrl_render`。

## 5. 各单元职责与依赖

| 单元 | 职责 | 依赖 |
|---|---|---|
| `occtcontour`(库) | `loadCompound`(STEP→Compound)、`computeOuterContour`、`mapViewToWorld` | 仅 OCCT(TKHLR/TKBO/TKShHealing/TKMesh/TKTopAlgo/TKDESTEP/TKBRep/…) |
| `contour_sweep`(exe) | 遍历目录算外轮廓,记录 ok/fail+耗时 | `occtcontour` |
| `contour_svg`(exe) | 算外轮廓导出 SVG(`--defl` 控精度) | `occtcontour` |
| `contour_tests`(exe) | 解析验证 + 复杂件回归 | `occtcontour` |
| `ocrl_render`(库) | `buildOutlineNode` + 反向Z线管线 | `occtcontour` + `vsg::vsg` |
| `contour_viewer`(exe) | 实体+轮廓 viewer,键盘交互 | `ocrl_render` + `vsgocct::vsgocct` |

## 6. 关键技术改动:ModelLoad 去 vsgocct

`contour/src/ModelLoad.cpp` 改为:

```cpp
#include "occtcontour/ModelLoad.h"
#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <fstream>
#include <stdexcept>

namespace ocrl {
TopoDS_Compound loadCompound(const std::filesystem::path& stepFile)
{
    std::ifstream input(stepFile, std::ios::binary);          // ifstream 兼容中文/Unicode 路径
    if (!input) throw std::runtime_error("无法打开 STEP: " + stepFile.u8string());

    STEPControl_Reader reader;
    if (reader.ReadStream(stepFile.filename().u8string().c_str(), input) != IFSelect_RetDone)
        throw std::runtime_error("OCCT 读取 STEP 失败: " + stepFile.u8string());
    reader.TransferRoots();

    TopoDS_Compound out; BRep_Builder b; b.MakeCompound(out);
    for (int i = 1; i <= reader.NbShapes(); ++i)
        if (!reader.Shape(i).IsNull()) b.Add(out, reader.Shape(i));
    return out;
}
} // namespace ocrl
```

说明:`STEPControl_Reader::ReadStream` 用流读取以兼容中文路径(与原 vsgocct 同思路);
`TransferRoots` + `Shape(i)` 汇成 Compound(等价于原"读+拍平")。装配 location 由 STEP 内部
处理。颜色/材质/装配树**不再在算法侧解析**(算法只需几何);渲染侧仍用 vsgocct 拿这些。
实现时若 `ReadStream` 签名/可用性与本机 OCCT 不符,回退用 `ReadFile`(并对非 ASCII 路径
先拷到临时 ASCII 名)——以编译/测试为准确认。

## 7. CMake 设计

### 7.1 `contour/CMakeLists.txt`(可独立 + 可被 add_subdirectory)
- `project(occtcontour CXX)`;C++17;`-DNOMINMAX`;强制 Debug 配置映射(同现状)。
- 找 OCCT:沿用 `include(D:/OCCT/build/OpenCASCADETargets.cmake)` + `OpenCASCADE_INCLUDE_DIR`。
- `add_library(occtcontour STATIC src/OuterContour.cpp src/ModelLoad.cpp)`,`add_library(occtcontour::occtcontour ALIAS occtcontour)`。
- `target_include_directories(occtcontour PUBLIC $<BUILD_INTERFACE:include> $<INSTALL_INTERFACE:include> ${OpenCASCADE_INCLUDE_DIR})`。
- `target_link_libraries(occtcontour PUBLIC ${OCRL_OCCT_LIBS})`(TKHLR TKBO TKShHealing TKMesh TKDESTEP TKTopAlgo TKBRep TKGeomAlgo TKGeomBase TKG3d TKG2d TKMath TKernel)。
- DLL 拷贝 helper、apps、tests、`enable_testing()`。
- 安装导出:`install(TARGETS occtcontour EXPORT occtcontourTargets ...)`、`install(DIRECTORY include/ ...)`、
  `install(EXPORT occtcontourTargets NAMESPACE occtcontour:: ...)`、由 `occtcontourConfig.cmake.in`
  生成 Config(内部 `include(OpenCASCADETargets)` 以带出 OCCT 依赖)。
- 提供选项 `OCCTCONTOUR_BUILD_APPS`/`OCCTCONTOUR_BUILD_TESTS`(默认 ON;被上层 add_subdirectory 时可关)。

### 7.2 `render/CMakeLists.txt`
- `project(ocrl_render CXX)`;`find_package(vsg ...)`;`add_subdirectory(D:/vsgOcct/src ...)`(复用 vsgocct)。
- 若 `TARGET occtcontour::occtcontour` 不存在(独立编译 render)则 `find_package(occtcontour REQUIRED)`。
- `ocrl_render` 库 + `contour_viewer` exe,链 `occtcontour::occtcontour vsg::vsg vsgocct::vsgocct`。

### 7.3 根 `CMakeLists.txt`(super-build)
- `option(OCRL_BUILD_RENDER "Build the VSG render project" ON)`。
- `add_subdirectory(contour)`;`if(OCRL_BUILD_RENDER) add_subdirectory(render) endif()`。
- 只编算法:`cmake -S . -B build -DOCRL_BUILD_RENDER=OFF`。

## 7.4 头文件路径迁移
- `#include "OuterContour.h"` → `#include <occtcontour/OuterContour.h>`(库公共头放 `contour/include/occtcontour/`)。
- 测试/apps/render 改为该路径。C++ 命名空间 `ocrl` 不变,源码改动最小。

## 8. 验证(完成定义)

1. **超级构建**:`cmake -S . -B build -G "Visual Studio 18 2026" -A x64` + build → 0 错误,产出
   `occtcontour` 库、contour_tests/sweep/svg、contour_viewer。
2. **只编算法**:`cmake -S . -B build_algo -DOCRL_BUILD_RENDER=OFF` → 不触发 vsg/Qt 查找,
   构建出算法库 + 纯 OCCT 工具/测试。`ctest` 全过(cylinder=4π、knife 外环 198 等不变)。
3. **算法库零 vsg**:`occtcontour` 的链接项里无 vsg/vsgocct(用 `dumpbin/depends` 或检查 CMake 链接行确认)。
4. **find_package 消费**:`install` 到一个 prefix,写一个最小外部 CMake 工程 `find_package(occtcontour)`
   + 链接 + 调 `computeOuterContour`,能编译运行(在 `contour/examples/consumer_smoke/` 放一个最小消费示例并文档化)。
5. **add_subdirectory 消费**:render 侧即是 add_subdirectory 消费的现成证明(超级构建)。
6. **渲染回归**:`contour_viewer --frames 5` 仍跑通(knife loops=10)。
7. **GitHub**:推送更新。

## 9. 明确不做(YAGNI)

- 不拆成两个独立 git 仓库(用户要"一个仓库")。
- 不引入 vcpkg/conan 打包。
- 算法侧不解析颜色/材质/装配语义(只要几何);需要语义的渲染侧用 vsgocct。
- 不改外轮廓算法本身(刚修好的区域并集保持不变)。

## 10. 迁移步骤概览(详见 writing-plans)

移动文件 → 改头文件 include 路径 → 重写三处 CMake(contour/render/root)→ 改 ModelLoad 为 OCCT 读取 →
超级构建+ctest 全过 → 只编算法验证 → 写最小 find_package 消费示例验证 → 更新 README → 推送。
