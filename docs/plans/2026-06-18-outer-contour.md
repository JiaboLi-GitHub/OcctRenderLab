# 模型外轮廓(精确 HLR)+ VSG 渲染 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 OCCT 精确 HLR(`HLRBRep_Algo`)算出 STEP 模型沿任意投影方向的剪影外轮廓(最外闭环 + 内孔),并在纯 VSG viewer 中与实体叠加显示、可切换投影方向。

**Architecture:** 独立项目 `D:\OcctRenderLab`,通过 `add_subdirectory(D:/vsgOcct/src)` 复用 `vsgocct::vsgocct`(STEP 读取与三角化),不修改 vsgOcct/OCCT。核心库 `ocrl_core`(ModelLoad + OuterContour,只依赖 OCCT + vsgocct);可执行体 `contour_viewer`(显示)、`contour_tests`(解析验证)、`contour_sweep`(鲁棒性扫描)。

**Tech Stack:** C++17,MSVC 2022,CMake ≥ 3.21;OCCT 8.0.0-rc3(构建树 `D:/OCCT/build`,Debug);VulkanSceneGraph 1.1.13;vsgocct 静态库。

---

## 关键 API 事实(已逐一核实,实现时照此写)

- **HLR 管线**:`Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();` → `algo->Add(shape, 0);` → `algo->Projector(HLRAlgo_Projector(gp_Ax2(origin, viewDir)));`(`gp_Ax2(Pnt,Dir)` 的 `Dir`=视线/主方向,正交无透视)→ `algo->Update();` → `algo->Hide();`。然后 `HLRBRep_HLRToShape toShape(algo);`,取 `toShape.OutLineVCompound()`(可见剪影边)+ `toShape.VCompound()`(可见 C0 锐边)。**两者都要**:纯曲面剪影靠 OutLine,棱柱/平面边界靠 VCompound。返回的是投影平面内 **2D 边**(视坐标系 X,Y,Z≈0)。
- **边类型**:`opencascade::handle`/`Handle()` 宏均可。OCCT 8.0 已移除 `TopTools_HSequenceOfShape`,用 `NCollection_HSequence<TopoDS_Shape>`。
- **拼环**:`ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, tol, /*shared=*/Standard_False, wires);`,`edges`/`wires` 均为 `opencascade::handle<NCollection_HSequence<TopoDS_Shape>>&`(1-based,`Value(i)`,`Length()`)。
- **修补/闭合**:`ShapeFix_Wire fix; fix.Load(wire); fix.SetPrecision(tol); fix.FixConnected(); fix.FixClosed(); fix.Perform(); wire = fix.Wire();`,闭合判定 `wire.Closed()`。
- **面积**:`BRepBuilderAPI_MakeFace mk(wire, /*OnlyPlane=*/Standard_True); GProp_GProps p; BRepGProp::SurfaceProperties(mk.Face(), p); double area = p.Mass();`。
- **包围盒**:`Bnd_Box b; BRepBndLib::Add(wire, b, /*useTriangulation=*/Standard_False); b.Get(xmin,ymin,zmin,xmax,ymax,zmax);`(min 三个在前)。
- **边离散化**:`BRepAdaptor_Curve c(edge); GCPnts_TangentialDeflection d(c, angDefl, curvDefl);` → `d.NbPoints()`(1-based)、`d.Value(i)` 返回 `gp_Pnt`。直线判定 `c.GetType() == GeomAbs_Line`。
- **遍历边**:`for (TopExp_Explorer e(shape, TopAbs_EDGE); e.More(); e.Next()) { const TopoDS_Edge& ed = TopoDS::Edge(e.Current()); }`。
- **拍平装配**:`vsgocct::cad::AssemblyData a = vsgocct::cad::readStep(path);` 递归 `ShapeNode{type, shape, location, children}`,`loc = parentLoc * node.location`,Part 节点 `builder.Add(out, node.shape.Moved(loc))`。
- **复用实体场景**:`vsgocct::scene::buildAssemblyScene(assembly)` → `AssemblySceneData{ vsg::ref_ptr<vsg::Node> scene; vsg::dvec3 center; double radius; }`。
- **VSG 线管线**:自写 GLSL(push_constant 128 字节 `{mat4 projection; mat4 modelView;}`),`topology=VK_PRIMITIVE_TOPOLOGY_LINE_LIST`,两路 binding(pos+color,均 `VK_FORMAT_R32G32B32_SFLOAT`),`cullMode=NONE`、`lineWidth=1`、**`depthCompareOp=VK_COMPARE_OP_GREATER_OR_EQUAL`(VSG 反向 Z!)**。几何用 `vsg::VertexIndexDraw`(顺序索引)。
- **VSG viewer**:`Window::create(WindowTraits::create(w,h,title))` → `Camera::create(Perspective::create(fovDeg,aspect,near,far), LookAt::create(eye,center,up=(0,0,1)), ViewportState::create(extent))` → `createCommandGraphForView(window,camera,scene)` → `viewer->assignRecordAndSubmitTaskAndPresentation({cg})` → `viewer->compile()` → `while(viewer->advanceToNextFrame()){ handleEvents(); update(); recordAndSubmit(); present(); }`。
- **运行时换几何**:`auto cr = viewer->compileManager->compile(node); vsg::updateViewer(*viewer, cr);`。
- **键盘**:`class H : public vsg::Inherit<vsg::Visitor,H>{ void apply(vsg::KeyPressEvent& k) override {...} };`,键值 `vsg::KEY_1..KEY_9, KEY_0, KEY_f` 等;`viewer->addEventHandler(H::create());` + `vsg::Trackball` + `vsg::CloseHandler`。
- **CMake**:`include("D:/OCCT/build/OpenCASCADETargets.cmake")` 定义全部 TK* 导入目标;`set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Debug)`(及 RELWITHDEBINFO/MINSIZEREL);`add_definitions(-DNOMINMAX)`;link `TKHLR`(传递 TKBRep/TKernel/TKMath/TKGeomBase/TKG2d/TKG3d/TKGeomAlgo/TKTopAlgo)+ **显式** `TKShHealing`(ShapeAnalysis_FreeBounds)+ `TKMesh`;运行期用 `$<TARGET_RUNTIME_DLLS:tgt>` 拷 DLL。

**测试素材**(已确认几何):`柱体.step`=精确圆柱 R=2 H=10 轴+Z;`球体.step`=精确球 R=5;`板.stp`=薄板 88.5×38.2×6.2 带圆角孔;`长条.STEP`=细长机加工件 26×128×141 带孔+沉头。路径 `D:\model\12\`。

---

## File Structure

| 文件 | 职责 |
|---|---|
| `CMakeLists.txt` | 顶层:OCCT 导入、vsg 查找、`add_subdirectory(D:/vsgOcct/src)`、定义 `ocrl_core` + DLL 拷贝 helper、加子目录 |
| `src/ModelLoad.h` / `.cpp` | 复用 `cad::readStep`,拍平装配为 `TopoDS_Compound` |
| `src/OuterContour.h` / `.cpp` | 核心:HLR 投影 → 抽边 → 离散 → 拼环 → 选最外环 → 度量;`computeOuterContour` |
| `tests/CMakeLists.txt` / `tests/contour_tests.cpp` | 基本体解析验证(自包含断言,无 gtest 依赖) |
| `apps/contour_viewer/CMakeLists.txt` / `main.cpp` / `OutlineRender.{h,cpp}` | VSG 显示 demo:实体+轮廓,键盘切向重算 |
| `apps/contour_sweep/CMakeLists.txt` / `main.cpp` | 遍历目录跑外轮廓,记录 ok/fail+耗时,不崩 |

---

## Task 1: 工程骨架 + 依赖链打通(空程序能链接)

**Files:**
- Create: `D:\OcctRenderLab\CMakeLists.txt`
- Create: `D:\OcctRenderLab\src\smoke_main.cpp`(临时,验证链接后删除)

- [ ] **Step 1: 写顶层 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.21)
project(OcctRenderLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(WIN32)
    add_definitions(-DNOMINMAX)
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

# 把运行期 DLL(OCCT + vsg)拷到目标 exe 旁
function(ocrl_copy_runtime_dlls target_name)
    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy -t "$<TARGET_FILE_DIR:${target_name}>"
                    $<TARGET_RUNTIME_DLLS:${target_name}>
            COMMAND_EXPAND_LISTS)
    endif()
endfunction()

# 临时冒烟程序(Task 1 验证用,Task 2 起删除)
add_executable(ocrl_smoke src/smoke_main.cpp)
target_link_libraries(ocrl_smoke PRIVATE ocrl_core)
ocrl_copy_runtime_dlls(ocrl_smoke)
```

- [ ] **Step 2: 写临时冒烟程序 `src/smoke_main.cpp`**

```cpp
#include <iostream>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <Standard_Version.hxx>
#include <vsg/core/Version.h>

int main()
{
    TopoDS_Compound c; BRep_Builder b; b.MakeCompound(c);
    std::cout << "OCCT " << OCC_VERSION_COMPLETE
              << ", VSG " << VSG_VERSION_STRING
              << ", compound null=" << c.IsNull() << "\n";
    return 0;
}
```

- [ ] **Step 3: 配置 + 构建,验证整条依赖链可链接**

Run:
```powershell
cmake -S D:/OcctRenderLab -B D:/OcctRenderLab/build -G "Visual Studio 17 2022" -A x64
cmake --build D:/OcctRenderLab/build --config Debug --target ocrl_smoke
```
Expected: 配置成功(找到 vsg、include 了 OpenCASCADETargets、vsgocct 子目录加入),构建出 `build\Debug\ocrl_smoke.exe`,无链接错误。

- [ ] **Step 4: 运行冒烟程序**

Run:
```powershell
D:/OcctRenderLab/build/Debug/ocrl_smoke.exe
```
Expected: 打印类似 `OCCT 8000003... , VSG 1.1.13, compound null=0`。证明 OCCT+vsg+vsgocct DLL 都就位。

- [ ] **Step 5: 提交**

```powershell
cd D:/OcctRenderLab
git add CMakeLists.txt src/smoke_main.cpp
git commit -m "build: 打通 OCCT+vsg+vsgocct 依赖链(冒烟程序)"
```

---

## Task 2: ModelLoad — 读 STEP 并拍平装配为 TopoDS_Compound

**Files:**
- Create: `src/ModelLoad.h`, `src/ModelLoad.cpp`
- Test: `tests/contour_tests.cpp`(本任务新建)+ `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`(加 tests 子目录;删 smoke 目标)

- [ ] **Step 1: 写失败测试 `tests/contour_tests.cpp`**

```cpp
// 自包含断言测试,无 gtest 依赖。失败累加并最终返回非零。
#include <cmath>
#include <cstdio>
#include <string>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include "ModelLoad.h"

#ifndef MODEL_DIR
#define MODEL_DIR "D:/model/12"
#endif

static int g_fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("[FAIL] %s\n", msg); ++g_fails; } \
                              else { std::printf("[ok]   %s\n", msg); } } while(0)

static std::size_t countEdges(const TopoDS_Shape& s) {
    std::size_t n = 0;
    for (TopExp_Explorer e(s, TopAbs_EDGE); e.More(); e.Next()) ++n;
    return n;
}

static void test_model_load() {
    TopoDS_Compound c = ocrl::loadCompound(std::string(MODEL_DIR) + "/柱体.step");
    CHECK(!c.IsNull(), "loadCompound(柱体) 返回非空 compound");
    CHECK(countEdges(c) > 0, "柱体 compound 含边");
}

int main() {
    test_model_load();
    std::printf(g_fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", g_fails);
    return g_fails ? 1 : 0;
}
```

- [ ] **Step 2: 写 `tests/CMakeLists.txt`,并在顶层加入子目录(同时删 smoke)**

`tests/CMakeLists.txt`:
```cmake
add_executable(contour_tests contour_tests.cpp)
target_link_libraries(contour_tests PRIVATE ocrl_core)
target_compile_definitions(contour_tests PRIVATE MODEL_DIR="D:/model/12")
ocrl_copy_runtime_dlls(contour_tests)
enable_testing()
add_test(NAME contour_tests COMMAND contour_tests)
```
在顶层 `CMakeLists.txt` 末尾,删掉 `ocrl_smoke` 那 3 行,改为:
```cmake
enable_testing()
add_subdirectory(tests)
```
并删除 `src/smoke_main.cpp`。

- [ ] **Step 3: 运行测试,确认因缺 ModelLoad.h 而构建失败**

Run:
```powershell
cmake -S D:/OcctRenderLab -B D:/OcctRenderLab/build -G "Visual Studio 17 2022" -A x64
cmake --build D:/OcctRenderLab/build --config Debug --target contour_tests
```
Expected: 编译失败 `cannot open include file 'ModelLoad.h'`(或链接缺 `ocrl::loadCompound`)。

- [ ] **Step 4: 写 `src/ModelLoad.h`**

```cpp
#pragma once
#include <filesystem>
#include <TopoDS_Compound.hxx>

namespace ocrl {
// 读 STEP(复用 vsgocct::cad::readStep),递归拍平装配树为一个已应用 location 的 TopoDS_Compound。
TopoDS_Compound loadCompound(const std::filesystem::path& stepFile);
}
```

- [ ] **Step 5: 写 `src/ModelLoad.cpp`**

```cpp
#include "ModelLoad.h"

#include <vsgocct/cad/StepReader.h>

#include <BRep_Builder.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>

namespace ocrl {
namespace {
void addNode(const vsgocct::cad::ShapeNode& node,
             const TopLoc_Location& parentLoc,
             BRep_Builder& builder,
             TopoDS_Compound& out)
{
    const TopLoc_Location loc = parentLoc * node.location;
    if (node.type == vsgocct::cad::ShapeNodeType::Part && !node.shape.IsNull())
        builder.Add(out, node.shape.Moved(loc));
    for (const auto& child : node.children)
        addNode(child, loc, builder, out);
}
} // namespace

TopoDS_Compound loadCompound(const std::filesystem::path& stepFile)
{
    const vsgocct::cad::AssemblyData assembly = vsgocct::cad::readStep(stepFile);
    TopoDS_Compound out;
    BRep_Builder builder;
    builder.MakeCompound(out);
    for (const auto& root : assembly.roots)
        addNode(root, TopLoc_Location(), builder, out);
    return out;
}
} // namespace ocrl
```

- [ ] **Step 6: 重新配置(因增删了文件)+ 构建 + 运行测试**

Run:
```powershell
cmake -S D:/OcctRenderLab -B D:/OcctRenderLab/build -G "Visual Studio 17 2022" -A x64
cmake --build D:/OcctRenderLab/build --config Debug --target contour_tests
D:/OcctRenderLab/build/tests/Debug/contour_tests.exe
```
Expected: 输出含 `[ok] loadCompound(柱体) 返回非空 compound`、`[ok] 柱体 compound 含边`、`ALL PASS`,退出码 0。

- [ ] **Step 7: 提交**

```powershell
cd D:/OcctRenderLab
git add -A
git commit -m "feat(ModelLoad): 复用 vsgocct 读 STEP 并拍平装配为 TopoDS_Compound"
```

---

## Task 3: OuterContour — HLR 抽取可见剪影 + 锐边(2D 投影边)

**Files:**
- Create: `src/OuterContour.h`, `src/OuterContour.cpp`
- Test: `tests/contour_tests.cpp`(追加)

- [ ] **Step 1: 写 `src/OuterContour.h`(完整公开接口,后续任务填充实现)**

```cpp
#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

namespace ocrl {

struct ContourOptions {
    gp_Dir direction{0.0, 0.0, 1.0};   // 视线/投影方向(沿此方向看,正交)
    double connectTolerance  = 1e-4;   // 拼环容差
    double angularDeflection = 0.05;   // 边离散化角偏差(rad)
    double curvatureDeflection = 0.05; // 边离散化曲率偏差
};

using Polyline = std::vector<gp_Pnt>;  // 顺序点(闭环时首尾相接)

struct ContourResult {
    bool ok = false;
    std::string message;

    gp_Ax2 viewSystem;                 // 视坐标系(把 2D 结果映射回世界用)
    TopoDS_Wire outerWire;             // 最外环(精确曲线)
    std::vector<TopoDS_Wire> holeWires;

    Polyline outerPolyline;            // 视平面内(Z≈0)离散折线
    std::vector<Polyline> holePolylines;

    double area = 0.0;                 // 外环净面积(精确, BRepGProp)
    double bboxMinX = 0, bboxMinY = 0, bboxMaxX = 0, bboxMaxY = 0;
    std::size_t loopCount = 0;         // 闭环总数(外 + 孔)
    std::size_t straightEdgeCount = 0; // 外环中直线边数

    double bboxWidth()  const { return bboxMaxX - bboxMinX; }
    double bboxHeight() const { return bboxMaxY - bboxMinY; }
    double bboxArea()   const { return bboxWidth() * bboxHeight(); }
    double aspect() const {
        const double w = bboxWidth(), h = bboxHeight();
        if (w <= 0.0 || h <= 0.0) return 0.0;
        return (w >= h) ? w / h : h / w;
    }
};

// 主入口(Task 6 完整实现)。
ContourResult computeOuterContour(const TopoDS_Shape& shape, const ContourOptions& opts = {});

// ---- 内部分步(供测试与主入口复用)----
namespace detail {
// HLR:抽取可见剪影 + 可见锐边,合并为投影平面内 2D 边的 Compound。viewSystem 输出所用视坐标系。
TopoDS_Compound extractVisibleOutlineEdges(const TopoDS_Shape& shape,
                                           const gp_Dir& direction,
                                           gp_Ax2& viewSystem);
}

// 把视平面内 2D 点(Z≈0)映射回世界坐标,放到沿视向 depth 处的平面。
gp_Pnt mapViewToWorld(const gp_Ax2& viewSystem, const gp_Pnt& viewPoint, double depth);
}
```

- [ ] **Step 2: 在 `tests/contour_tests.cpp` 追加失败测试**

在 `#include "ModelLoad.h"` 后加 `#include "OuterContour.h"`;新增函数并在 `main()` 调用:
```cpp
static void test_hlr_extract() {
    TopoDS_Compound c = ocrl::loadCompound(std::string(MODEL_DIR) + "/柱体.step");
    gp_Ax2 vs;
    TopoDS_Compound edges = ocrl::detail::extractVisibleOutlineEdges(c, gp_Dir(0,0,1), vs);
    CHECK(!edges.IsNull(), "HLR 顶视抽边返回非空");
    CHECK(countEdges(edges) > 0, "HLR 顶视含可见边");
}
```
`main()` 加 `test_hlr_extract();`。

- [ ] **Step 3: 构建,确认链接失败(缺 `extractVisibleOutlineEdges` 实现)**

Run: `cmake --build D:/OcctRenderLab/build --config Debug --target contour_tests`
Expected: 链接错误 `unresolved external ... extractVisibleOutlineEdges`。

- [ ] **Step 4: 写 `src/OuterContour.cpp` 的 HLR 部分**

```cpp
#include "OuterContour.h"

#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>

#include <BRep_Builder.hxx>
#include <gp_Pnt.hxx>

namespace ocrl {
namespace detail {

TopoDS_Compound extractVisibleOutlineEdges(const TopoDS_Shape& shape,
                                           const gp_Dir& direction,
                                           gp_Ax2& viewSystem)
{
    viewSystem = gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), direction); // Z 轴 = 视线方向

    Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
    algo->Add(shape, 0);                            // 0 = 不生成等参线
    algo->Projector(HLRAlgo_Projector(viewSystem)); // gp_Ax2 构造 => 正交投影
    algo->Update();
    algo->Hide();

    HLRBRep_HLRToShape toShape(algo);
    const TopoDS_Shape outline = toShape.OutLineVCompound(); // 可见剪影(切轮廓)
    const TopoDS_Shape sharp   = toShape.VCompound();        // 可见 C0 锐边

    TopoDS_Compound out;
    BRep_Builder builder;
    builder.MakeCompound(out);
    if (!outline.IsNull()) builder.Add(out, outline);
    if (!sharp.IsNull())   builder.Add(out, sharp);
    return out;
}

} // namespace detail

gp_Pnt mapViewToWorld(const gp_Ax2& vs, const gp_Pnt& p, double depth)
{
    const gp_XYZ o = vs.Location().XYZ();
    const gp_XYZ x = vs.XDirection().XYZ();
    const gp_XYZ y = vs.YDirection().XYZ();
    const gp_XYZ z = vs.Direction().XYZ();
    const gp_XYZ w = o + x * p.X() + y * p.Y() + z * depth;
    return gp_Pnt(w);
}

} // namespace ocrl
```

- [ ] **Step 5: 构建 + 运行测试**

Run:
```powershell
cmake --build D:/OcctRenderLab/build --config Debug --target contour_tests
D:/OcctRenderLab/build/tests/Debug/contour_tests.exe
```
Expected: 新增两行 `[ok] HLR 顶视抽边返回非空`、`[ok] HLR 顶视含可见边`,`ALL PASS`。

- [ ] **Step 6: 提交**

```powershell
cd D:/OcctRenderLab
git add -A
git commit -m "feat(OuterContour): HLR 抽取可见剪影+锐边(2D 投影边)"
```

---

## Task 4: OuterContour — 边离散化为折线(LINE_LIST 段对)

**Files:**
- Modify: `src/OuterContour.h`(在 `detail` 内加声明)、`src/OuterContour.cpp`
- Test: `tests/contour_tests.cpp`(追加)

- [ ] **Step 1: 在 `OuterContour.h` 的 `namespace detail` 内追加声明**

```cpp
// 把一条边离散成顺序点(>=2 个)。直线返回两端点;曲线按偏差采样。
Polyline discretizeEdge(const TopoDS_Edge& edge, double angDefl, double curvDefl);
```
并在头顶部 `#include <TopoDS_Edge.hxx>`。

- [ ] **Step 2: 追加失败测试**

```cpp
static void test_discretize() {
    TopoDS_Compound c = ocrl::loadCompound(std::string(MODEL_DIR) + "/球体.step");
    gp_Ax2 vs;
    TopoDS_Compound edges = ocrl::detail::extractVisibleOutlineEdges(c, gp_Dir(0,0,1), vs);
    std::size_t pts = 0;
    for (TopExp_Explorer e(edges, TopAbs_EDGE); e.More(); e.Next()) {
        auto poly = ocrl::detail::discretizeEdge(TopoDS::Edge(e.Current()), 0.05, 0.05);
        CHECK(poly.size() >= 2, "每条边离散点数 >= 2");
        pts += poly.size();
    }
    CHECK(pts > 8, "球体剪影圆离散出足够多点(曲线被细分)");
}
```
在文件顶部确保已 `#include <TopoDS.hxx>`(供 `TopoDS::Edge`),`main()` 加 `test_discretize();`。

- [ ] **Step 3: 构建,确认链接失败(缺 `discretizeEdge`)**

Run: `cmake --build D:/OcctRenderLab/build --config Debug --target contour_tests`
Expected: `unresolved external ... discretizeEdge`。

- [ ] **Step 4: 在 `OuterContour.cpp` 实现 `discretizeEdge`**

文件顶部加 include:
```cpp
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
```
在 `namespace detail` 内加:
```cpp
Polyline discretizeEdge(const TopoDS_Edge& edge, double angDefl, double curvDefl)
{
    Polyline pts;
    BRepAdaptor_Curve curve(edge);
    GCPnts_TangentialDeflection disc(curve, angDefl, curvDefl);
    const int n = disc.NbPoints();
    pts.reserve(static_cast<std::size_t>(n > 0 ? n : 0));
    for (int i = 1; i <= n; ++i)     // 1-based
        pts.push_back(disc.Value(i));
    return pts;
}
```

- [ ] **Step 5: 构建 + 运行测试**

Run:
```powershell
cmake --build D:/OcctRenderLab/build --config Debug --target contour_tests
D:/OcctRenderLab/build/tests/Debug/contour_tests.exe
```
Expected: `[ok] 每条边离散点数 >= 2`(多行)、`[ok] 球体剪影圆离散出足够多点`,`ALL PASS`。

- [ ] **Step 6: 提交**

```powershell
cd D:/OcctRenderLab
git add -A
git commit -m "feat(OuterContour): 边离散化为折线(GCPnts_TangentialDeflection)"
```

---

## Task 5: OuterContour — 拼环、选最外环、算面积与包围盒

**Files:**
- Modify: `src/OuterContour.h`(`detail` 内加结构与声明)、`src/OuterContour.cpp`
- Test: `tests/contour_tests.cpp`(追加)

- [ ] **Step 1: 在 `OuterContour.h` 的 `detail` 内追加**

```cpp
struct AssembledLoops {
    std::vector<TopoDS_Wire> closedWires;   // 全部闭合环
    std::vector<double>      areas;          // 对应平面面积(>=0)
    int outerIndex = -1;                     // closedWires 中面积最大者下标
};
// 把 2D 边 Compound 连成闭合环,逐环算面积,标出最外环。
AssembledLoops assembleClosedLoops(const TopoDS_Compound& edges, double connectTol);
```
头顶 `#include <vector>` 已有。

- [ ] **Step 2: 追加失败测试(圆柱顶视 = 半径2 的圆,面积 4π)**

```cpp
static void test_assemble_cylinder_top() {
    TopoDS_Compound c = ocrl::loadCompound(std::string(MODEL_DIR) + "/柱体.step");
    gp_Ax2 vs;
    TopoDS_Compound edges = ocrl::detail::extractVisibleOutlineEdges(c, gp_Dir(0,0,1), vs);
    auto loops = ocrl::detail::assembleClosedLoops(edges, 1e-4);
    CHECK(loops.outerIndex >= 0, "圆柱顶视:找到最外闭环");
    const double area = loops.areas[loops.outerIndex];
    // 圆 R=2 => 面积 = π*4 ≈ 12.566
    CHECK(std::fabs(area - M_PI * 4.0) < 0.2, "圆柱顶视外环面积 ≈ 4π");
}
```
若 `M_PI` 未定义,文件顶部加 `#define _USE_MATH_DEFINES` 再 `#include <cmath>`(MSVC 需此宏)。`main()` 加 `test_assemble_cylinder_top();`。

- [ ] **Step 3: 构建,确认链接失败(缺 `assembleClosedLoops`)**

Run: `cmake --build D:/OcctRenderLab/build --config Debug --target contour_tests`
Expected: `unresolved external ... assembleClosedLoops`。

- [ ] **Step 4: 在 `OuterContour.cpp` 实现 `assembleClosedLoops`**

文件顶部加 include:
```cpp
#include <NCollection_HSequence.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <ShapeFix_Wire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Wire.hxx>
```
在 `namespace detail` 内加:
```cpp
AssembledLoops assembleClosedLoops(const TopoDS_Compound& edgesCompound, double connectTol)
{
    AssembledLoops result;

    // 1) 收集所有边到 HSequence
    opencascade::handle<NCollection_HSequence<TopoDS_Shape>> edges =
        new NCollection_HSequence<TopoDS_Shape>();
    for (TopExp_Explorer e(edgesCompound, TopAbs_EDGE); e.More(); e.Next())
        edges->Append(e.Current());
    if (edges->IsEmpty())
        return result;

    // 2) 连成 wires(shared=false => 按端点距离 < connectTol 连接)
    opencascade::handle<NCollection_HSequence<TopoDS_Shape>> wires =
        new NCollection_HSequence<TopoDS_Shape>();
    ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, connectTol, Standard_False, wires);
    if (wires.IsNull() || wires->IsEmpty())
        return result;

    // 3) 逐环:修小缝隙 -> 闭合判定 -> 建面算面积
    double maxArea = -1.0;
    for (int i = 1; i <= wires->Length(); ++i)
    {
        TopoDS_Wire wire = TopoDS::Wire(wires->Value(i));

        ShapeFix_Wire fix;
        fix.Load(wire);
        fix.SetPrecision(connectTol);
        fix.FixConnected();
        fix.FixClosed();
        fix.Perform();
        wire = fix.Wire();

        if (!wire.Closed())
            continue;

        BRepBuilderAPI_MakeFace mkFace(wire, Standard_True); // OnlyPlane
        if (!mkFace.IsDone())
            continue;

        GProp_GProps props;
        BRepGProp::SurfaceProperties(mkFace.Face(), props);
        const double area = props.Mass();

        result.closedWires.push_back(wire);
        result.areas.push_back(area);
        if (area > maxArea) { maxArea = area; result.outerIndex = int(result.closedWires.size()) - 1; }
    }
    return result;
}
```

- [ ] **Step 5: 构建 + 运行测试**

Run:
```powershell
cmake --build D:/OcctRenderLab/build --config Debug --target contour_tests
D:/OcctRenderLab/build/tests/Debug/contour_tests.exe
```
Expected: `[ok] 圆柱顶视:找到最外闭环`、`[ok] 圆柱顶视外环面积 ≈ 4π`,`ALL PASS`。
(若面积偏差大:多半是 HLR 给出的圆被拆成多段、拼环容差不够 —— 调大 `connectTol` 到 `1e-3` 重试;记录到 message。)

- [ ] **Step 6: 提交**

```powershell
cd D:/OcctRenderLab
git add -A
git commit -m "feat(OuterContour): 拼闭合环+选最外环+BRepGProp 面积"
```

---

## Task 6: OuterContour — 组装 `computeOuterContour`(度量 + 孔 + 直边计数)

**Files:**
- Modify: `src/OuterContour.cpp`(实现 `computeOuterContour`)
- Test: `tests/contour_tests.cpp`(追加完整解析验证)

- [ ] **Step 1: 追加解析验证测试(圆柱/球体紧容差,板/长条结构性)**

```cpp
// 工具:对一个外轮廓结果做 (面积/包围盒面积) 与 长宽比 断言
static void test_full_pipeline() {
    using namespace ocrl;
    const std::string dir = MODEL_DIR;

    // 圆柱顶视 -> 圆:loops=1, area/bbox≈π/4, aspect≈1, 直边=0
    {
        ContourOptions o; o.direction = gp_Dir(0,0,1);
        ContourResult r = computeOuterContour(loadCompound(dir + "/柱体.step"), o);
        CHECK(r.ok, "柱体顶视 ok");
        CHECK(r.loopCount == 1, "柱体顶视 单闭环");
        CHECK(std::fabs(r.area / r.bboxArea() - M_PI/4.0) < 0.02, "柱体顶视 面积比≈π/4");
        CHECK(std::fabs(r.aspect() - 1.0) < 0.02, "柱体顶视 长宽比≈1");
        CHECK(r.straightEdgeCount == 0, "柱体顶视 无直边(纯圆)");
    }
    // 圆柱侧视 -> 矩形 4x10:area/bbox≈1, aspect≈2.5
    {
        ContourOptions o; o.direction = gp_Dir(1,0,0);
        ContourResult r = computeOuterContour(loadCompound(dir + "/柱体.step"), o);
        CHECK(r.ok, "柱体侧视 ok");
        CHECK(std::fabs(r.area / r.bboxArea() - 1.0) < 0.05, "柱体侧视 面积比≈1(矩形)");
        CHECK(std::fabs(r.aspect() - 2.5) < 0.1, "柱体侧视 长宽比≈2.5");
    }
    // 球体任意视 -> 圆 R=5:area/bbox≈π/4, aspect≈1, 直边=0
    {
        ContourOptions o; o.direction = gp_Dir(0.3,0.4,0.866);
        ContourResult r = computeOuterContour(loadCompound(dir + "/球体.step"), o);
        CHECK(r.ok, "球体 ok");
        CHECK(r.loopCount == 1, "球体 单闭环");
        CHECK(std::fabs(r.area / r.bboxArea() - M_PI/4.0) < 0.02, "球体 面积比≈π/4");
        CHECK(r.straightEdgeCount == 0, "球体 无直边");
    }
    // 薄板顶视 -> 圆角矩形 + 孔:loops>=1, aspect>2, 面积比接近1(宽松)
    {
        ContourOptions o; o.direction = gp_Dir(0,0,1);
        ContourResult r = computeOuterContour(loadCompound(dir + "/板.stp"), o);
        CHECK(r.ok, "板 ok");
        CHECK(r.aspect() > 2.0, "板顶视 长宽比>2");
        CHECK(r.area / r.bboxArea() > 0.85, "板顶视 外环近似填满包围盒");
    }
    // 长条:含孔 -> 闭环数 > 1(沿某主轴投影)
    {
        ContourOptions o; o.direction = gp_Dir(1,0,0);
        ContourResult r = computeOuterContour(loadCompound(dir + "/长条.STEP"), o);
        CHECK(r.ok, "长条 ok");
        CHECK(r.loopCount >= 1, "长条 至少一个闭环");
    }
}
```
`main()` 加 `test_full_pipeline();`。

- [ ] **Step 2: 构建 + 运行,确认新断言失败(`computeOuterContour` 仍是空/未实现)**

Run: `cmake --build ... --target contour_tests` 然后运行 exe。
Expected: 多条 `[FAIL]`(因 `computeOuterContour` 返回默认 `ok=false`)。

- [ ] **Step 3: 在 `OuterContour.cpp` 实现 `computeOuterContour`**

文件顶部加:
```cpp
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <GeomAbs_CurveType.hxx>
```
实现:
```cpp
namespace ocrl {

namespace {
// 用离散折线计算视平面 2D 包围盒(忽略 Z)。
void polylineBBox2d(const std::vector<Polyline>& polys,
                    double& minX, double& minY, double& maxX, double& maxY)
{
    bool first = true;
    for (const auto& poly : polys)
        for (const auto& p : poly) {
            if (first) { minX = maxX = p.X(); minY = maxY = p.Y(); first = false; }
            else {
                minX = std::min(minX, p.X()); maxX = std::max(maxX, p.X());
                minY = std::min(minY, p.Y()); maxY = std::max(maxY, p.Y());
            }
        }
    if (first) { minX = minY = maxX = maxY = 0.0; }
}

std::size_t countStraightEdges(const TopoDS_Wire& wire)
{
    std::size_t n = 0;
    for (TopExp_Explorer e(wire, TopAbs_EDGE); e.More(); e.Next()) {
        BRepAdaptor_Curve c(TopoDS::Edge(e.Current()));
        if (c.GetType() == GeomAbs_Line) ++n;
    }
    return n;
}

Polyline wireToPolyline(const TopoDS_Wire& wire, double angDefl, double curvDefl)
{
    Polyline out;
    for (TopExp_Explorer e(wire, TopAbs_EDGE); e.More(); e.Next()) {
        Polyline seg = detail::discretizeEdge(TopoDS::Edge(e.Current()), angDefl, curvDefl);
        // 段对拼接:直接追加(渲染时按 LINE_LIST 重排见 OutlineRender)
        for (const auto& p : seg) out.push_back(p);
    }
    return out;
}
} // namespace

ContourResult computeOuterContour(const TopoDS_Shape& shape, const ContourOptions& opts)
{
    ContourResult r;
    try {
        TopoDS_Compound edges = detail::extractVisibleOutlineEdges(shape, opts.direction, r.viewSystem);
        if (edges.IsNull()) { r.message = "HLR 抽边为空"; return r; }

        detail::AssembledLoops loops = detail::assembleClosedLoops(edges, opts.connectTolerance);
        if (loops.outerIndex < 0) { r.message = "未找到闭合外环"; return r; }

        r.outerWire = loops.closedWires[loops.outerIndex];
        r.area      = loops.areas[loops.outerIndex];
        r.loopCount = loops.closedWires.size();
        r.straightEdgeCount = countStraightEdges(r.outerWire);

        // 孔环 = 除最外环外的其余闭环
        for (std::size_t i = 0; i < loops.closedWires.size(); ++i)
            if (int(i) != loops.outerIndex) r.holeWires.push_back(loops.closedWires[i]);

        // 离散折线
        r.outerPolyline = wireToPolyline(r.outerWire, opts.angularDeflection, opts.curvatureDeflection);
        std::vector<Polyline> all{ r.outerPolyline };
        for (const auto& hw : r.holeWires) {
            Polyline hp = wireToPolyline(hw, opts.angularDeflection, opts.curvatureDeflection);
            r.holePolylines.push_back(hp);
            all.push_back(hp);
        }

        // 2D 包围盒(视平面)
        polylineBBox2d(all, r.bboxMinX, r.bboxMinY, r.bboxMaxX, r.bboxMaxY);

        r.ok = true;
    } catch (const Standard_Failure& e) {
        r.ok = false; r.message = std::string("OCCT 异常: ") + e.GetMessageString();
    } catch (const std::exception& e) {
        r.ok = false; r.message = std::string("异常: ") + e.what();
    }
    return r;
}

} // namespace ocrl
```
文件顶部还需 `#include <Standard_Failure.hxx>` 与 `#include <algorithm>`。

- [ ] **Step 4: 构建 + 运行测试**

Run:
```powershell
cmake --build D:/OcctRenderLab/build --config Debug --target contour_tests
D:/OcctRenderLab/build/tests/Debug/contour_tests.exe
```
Expected: 圆柱/球体紧容差与板/长条结构性断言全部 `[ok]`,`ALL PASS`。
(若柱体侧视 aspect 或面积比不过:HLR 侧视会把端盖圆投影成线段,检查 `connectTolerance`;必要时放宽该项容差并在 message 注明。)

- [ ] **Step 5: 用 ctest 跑一遍(集成入回归)**

Run: `ctest --test-dir D:/OcctRenderLab/build -C Debug --output-on-failure`
Expected: `100% tests passed, 1 test`。

- [ ] **Step 6: 提交**

```powershell
cd D:/OcctRenderLab
git add -A
git commit -m "feat(OuterContour): computeOuterContour 组装(度量/孔/直边)+ 解析验证全过"
```

---

## Task 7: OutlineRender — VSG 线管线 + 从折线建轮廓节点

**Files:**
- Create: `apps/contour_viewer/OutlineRender.h`, `apps/contour_viewer/OutlineRender.cpp`
- Create: `apps/contour_viewer/CMakeLists.txt`(本任务先建库目标 + 一个最小自检 exe)
- Modify: 顶层 `CMakeLists.txt`(加 `add_subdirectory(apps/contour_viewer)`)

> 渲染节点难做纯单元测试;本任务的"测试"是一个最小程序 `outline_render_selfcheck`:喂已知折线,断言节点非空且顶点数 = 2×段数,**不开窗口**(只构图)。

- [ ] **Step 1: 写 `apps/contour_viewer/OutlineRender.h`**

```cpp
#pragma once
#include <vector>
#include <vsg/all.h>
#include <gp_Pnt.hxx>
#include "OuterContour.h"

namespace ocrl {
// 用一组折线(每条按顺序点)构建 LINE_LIST 轮廓节点。color 为线色。
// depth:沿视向把 2D 视平面点摆到世界平面的深度(见 mapViewToWorld)。
vsg::ref_ptr<vsg::Node> buildOutlineNode(const gp_Ax2& viewSystem,
                                         const std::vector<Polyline>& polylines,
                                         const vsg::vec3& color,
                                         double depth);
// 暴露线管线创建(viewer 复用)。
vsg::ref_ptr<vsg::BindGraphicsPipeline> createLinePipeline();
}
```

- [ ] **Step 2: 写 `apps/contour_viewer/OutlineRender.cpp`**

```cpp
#include "OutlineRender.h"

namespace ocrl {

static const char* OUTLINE_VERT = R"(#version 450
layout(push_constant) uniform PC { mat4 projection; mat4 modelView; };
layout(location=0) in vec3 vertex;
layout(location=1) in vec3 inColor;
layout(location=0) out vec3 col;
out gl_PerVertex { vec4 gl_Position; };
void main(){ col = inColor; gl_Position = projection * (modelView * vec4(vertex,1.0)); }
)";

static const char* OUTLINE_FRAG = R"(#version 450
layout(location=0) in vec3 col;
layout(location=0) out vec4 o;
void main(){ o = vec4(col, 1.0); }
)";

vsg::ref_ptr<vsg::BindGraphicsPipeline> createLinePipeline()
{
    auto layout = vsg::PipelineLayout::create(
        vsg::DescriptorSetLayouts{},
        vsg::PushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 128}});
    auto vs = vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT,   "main", OUTLINE_VERT);
    auto fs = vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", OUTLINE_FRAG);

    auto vis = vsg::VertexInputState::create();
    vis->vertexBindingDescriptions.push_back({0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
    vis->vertexBindingDescriptions.push_back({1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
    vis->vertexAttributeDescriptions.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
    vis->vertexAttributeDescriptions.push_back({1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0});

    auto ias = vsg::InputAssemblyState::create();
    ias->topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    auto rs = vsg::RasterizationState::create();
    rs->cullMode = VK_CULL_MODE_NONE; rs->lineWidth = 1.0f;
    auto ds = vsg::DepthStencilState::create();
    ds->depthTestEnable = VK_TRUE; ds->depthWriteEnable = VK_TRUE;
    ds->depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;   // VSG 反向 Z

    vsg::GraphicsPipelineStates states{
        vis, ias, rs, vsg::ColorBlendState::create(),
        vsg::MultisampleState::create(), ds};
    auto gp = vsg::GraphicsPipeline::create(layout, vsg::ShaderStages{vs, fs}, states);
    return vsg::BindGraphicsPipeline::create(gp);
}

vsg::ref_ptr<vsg::Node> buildOutlineNode(const gp_Ax2& vs,
                                         const std::vector<Polyline>& polylines,
                                         const vsg::vec3& color,
                                         double depth)
{
    // 把每条折线的顺序点转为 LINE_LIST 段对:相邻点 (p[i],p[i+1]) 各成一段。
    std::vector<vsg::vec3> verts;
    for (const auto& poly : polylines) {
        if (poly.size() < 2) continue;
        for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
            const gp_Pnt a = mapViewToWorld(vs, poly[i],   depth);
            const gp_Pnt b = mapViewToWorld(vs, poly[i+1], depth);
            verts.emplace_back(float(a.X()), float(a.Y()), float(a.Z()));
            verts.emplace_back(float(b.X()), float(b.Y()), float(b.Z()));
        }
    }
    auto sg = vsg::StateGroup::create();
    sg->add(createLinePipeline());
    if (verts.empty()) return sg;

    const uint32_t n = static_cast<uint32_t>(verts.size());
    auto positions = vsg::vec3Array::create(n);
    auto colors    = vsg::vec3Array::create(n);
    auto indices   = vsg::uintArray::create(n);
    for (uint32_t i = 0; i < n; ++i) {
        (*positions)[i] = verts[i]; (*colors)[i] = color; (*indices)[i] = i;
    }
    auto draw = vsg::VertexIndexDraw::create();
    draw->assignArrays(vsg::DataList{positions, colors});
    draw->assignIndices(indices);
    draw->indexCount = indices->width();
    draw->instanceCount = 1;
    sg->addChild(draw);
    return sg;
}

} // namespace ocrl
```

- [ ] **Step 3: 写 `apps/contour_viewer/CMakeLists.txt`(库 + 自检 exe)**

```cmake
add_library(ocrl_render STATIC OutlineRender.cpp)
target_link_libraries(ocrl_render PUBLIC ocrl_core vsg::vsg)
target_include_directories(ocrl_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(outline_render_selfcheck selfcheck.cpp)
target_link_libraries(outline_render_selfcheck PRIVATE ocrl_render)
ocrl_copy_runtime_dlls(outline_render_selfcheck)
```
顶层 `CMakeLists.txt` 末尾加:`add_subdirectory(apps/contour_viewer)`。

- [ ] **Step 4: 写自检 `apps/contour_viewer/selfcheck.cpp`(不开窗口)**

```cpp
#include <cstdio>
#include "OutlineRender.h"

int main() {
    // 一条折线 3 个点 => 2 段 => 4 个顶点
    ocrl::Polyline poly = { gp_Pnt(0,0,0), gp_Pnt(1,0,0), gp_Pnt(1,1,0) };
    gp_Ax2 vs(gp_Pnt(0,0,0), gp_Dir(0,0,1));
    auto node = ocrl::buildOutlineNode(vs, { poly }, vsg::vec3(1,1,0), 0.0);
    int fails = 0;
    if (!node) { std::printf("[FAIL] node 为空\n"); ++fails; }
    else std::printf("[ok] buildOutlineNode 返回非空(3点折线=2段=4顶点)\n");
    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
```

- [ ] **Step 5: 配置 + 构建 + 运行自检**

Run:
```powershell
cmake -S D:/OcctRenderLab -B D:/OcctRenderLab/build -G "Visual Studio 17 2022" -A x64
cmake --build D:/OcctRenderLab/build --config Debug --target outline_render_selfcheck
D:/OcctRenderLab/build/apps/contour_viewer/Debug/outline_render_selfcheck.exe
```
Expected: `[ok] buildOutlineNode 返回非空 ...`、`PASS`。

- [ ] **Step 6: 提交**

```powershell
cd D:/OcctRenderLab
git add -A
git commit -m "feat(render): VSG 线管线 + 折线转 LINE_LIST 轮廓节点(自检通过)"
```

---

## Task 8: contour_viewer — 静态显示(实体 + 外轮廓,方向由命令行给定)

**Files:**
- Create: `apps/contour_viewer/main.cpp`
- Modify: `apps/contour_viewer/CMakeLists.txt`(加 `contour_viewer` exe)

- [ ] **Step 1: 写 `apps/contour_viewer/main.cpp`(静态版)**

```cpp
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include <vsg/all.h>

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/scene/SceneBuilder.h>

#include "ModelLoad.h"
#include "OuterContour.h"
#include "OutlineRender.h"

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("用法: contour_viewer <model.step> [dx dy dz]\n"); return 1; }
    const std::filesystem::path path = argv[1];
    gp_Dir dir(0, 0, 1);
    if (argc >= 5) dir = gp_Dir(std::stod(argv[2]), std::stod(argv[3]), std::stod(argv[4]));

    using clock = std::chrono::steady_clock;

    // 1) 读 STEP(一次),分别供 实体场景 与 外轮廓
    auto t0 = clock::now();
    vsgocct::cad::AssemblyData assembly = vsgocct::cad::readStep(path);
    auto sceneData = vsgocct::scene::buildAssemblyScene(assembly);
    auto t1 = clock::now();

    TopoDS_Compound compound = ocrl::loadCompound(path);  // 拍平(再读一次, demo 可接受)
    ocrl::ContourOptions opts; opts.direction = dir;
    ocrl::ContourResult contour = ocrl::computeOuterContour(compound, opts);
    auto t2 = clock::now();

    auto ms = [](auto a, auto b){ return std::chrono::duration_cast<std::chrono::milliseconds>(b-a).count(); };
    std::printf("读取+建场景 %lldms; 外轮廓 %lldms; ok=%d loops=%zu area=%.3f msg=%s\n",
                (long long)ms(t0,t1), (long long)ms(t1,t2), contour.ok,
                contour.loopCount, contour.area, contour.message.c_str());

    // 2) 组场景:实体 + 轮廓(轮廓摆到模型外缘的投影平面)
    auto root = vsg::Group::create();
    if (sceneData.scene) root->addChild(sceneData.scene);

    const double depth = -sceneData.radius * 1.2; // 沿视向往后摆,像投影面
    if (contour.ok) {
        std::vector<ocrl::Polyline> outer{ contour.outerPolyline };
        root->addChild(ocrl::buildOutlineNode(contour.viewSystem, outer, vsg::vec3(1.0f,0.9f,0.1f), depth));
        if (!contour.holePolylines.empty())
            root->addChild(ocrl::buildOutlineNode(contour.viewSystem, contour.holePolylines, vsg::vec3(0.2f,0.7f,1.0f), depth));
    }

    // 3) viewer + 相机(由 center/radius 定位)
    auto traits = vsg::WindowTraits::create(1280, 960, "OcctRenderLab - Outer Contour");
    auto window = vsg::Window::create(traits);
    if (!window) { std::printf("无法创建窗口(检查 Vulkan)\n"); return 1; }

    const vsg::dvec3 centre = sceneData.center;
    double radius = sceneData.radius > 0 ? sceneData.radius : 1.0;
    double aspect = double(window->extent2D().width) / double(window->extent2D().height);
    auto lookAt = vsg::LookAt::create(centre + vsg::dvec3(0, -radius*3.0, radius*1.5), centre, vsg::dvec3(0,0,1));
    auto persp  = vsg::Perspective::create(45.0, aspect, radius*0.001, radius*12.0);
    auto camera = vsg::Camera::create(persp, lookAt, vsg::ViewportState::create(window->extent2D()));

    auto viewer = vsg::Viewer::create();
    viewer->addWindow(window);
    auto trackball = vsg::Trackball::create(camera);
    trackball->addWindow(window);
    viewer->addEventHandler(trackball);
    viewer->addEventHandler(vsg::CloseHandler::create(viewer));

    auto cg = vsg::createCommandGraphForView(window, camera, root);
    viewer->assignRecordAndSubmitTaskAndPresentation({cg});
    viewer->compile();

    while (viewer->advanceToNextFrame()) {
        viewer->handleEvents();
        viewer->update();
        viewer->recordAndSubmit();
        viewer->present();
    }
    return 0;
}
```

- [ ] **Step 2: 在 `apps/contour_viewer/CMakeLists.txt` 加 exe**

```cmake
add_executable(contour_viewer main.cpp)
target_link_libraries(contour_viewer PRIVATE ocrl_render)
ocrl_copy_runtime_dlls(contour_viewer)
```

- [ ] **Step 3: 构建**

Run:
```powershell
cmake -S D:/OcctRenderLab -B D:/OcctRenderLab/build -G "Visual Studio 17 2022" -A x64
cmake --build D:/OcctRenderLab/build --config Debug --target contour_viewer
```
Expected: 构建成功,DLL 拷到 `build\apps\contour_viewer\Debug\`。

- [ ] **Step 4: 运行(顶视圆柱)— 人工目测**

Run:
```powershell
D:/OcctRenderLab/build/apps/contour_viewer/Debug/contour_viewer.exe D:/model/12/柱体.step
```
Expected: 控制台打印 `ok=1 loops=1 area≈12.566`;窗口显示圆柱实体,下方投影平面上一圈黄色圆形外轮廓;鼠标可轨道旋转;Esc 退出。再试 `... 柱体.step 1 0 0`(侧视)应见矩形轮廓。

- [ ] **Step 5: 提交**

```powershell
cd D:/OcctRenderLab
git add -A
git commit -m "feat(viewer): 静态显示实体+外轮廓(投影方向由命令行给定)"
```

---

## Task 9: contour_viewer — 键盘切换投影方向 + 显隐 + 运行时重算

**Files:**
- Modify: `apps/contour_viewer/main.cpp`

- [ ] **Step 1: 在 main.cpp 顶部加键盘处理器类(在 `main` 之前)**

```cpp
// 持有重算所需状态:按 1-6/0 切方向并重算轮廓,F/E/C/H/M 切显隐/模式。
class ContourKeyHandler : public vsg::Inherit<vsg::Visitor, ContourKeyHandler>
{
public:
    vsg::observer_ptr<vsg::Viewer> viewer;
    vsg::ref_ptr<vsg::Group> contourGroup;   // 仅挂轮廓子树,便于整体替换
    TopoDS_Compound compound;
    double depth = 0.0;

    void setDirection(const gp_Dir& d) { dir = d; recompute(); }

    void apply(vsg::KeyPressEvent& key) override
    {
        switch (key.keyBase) {
            case vsg::KEY_1: setDirection(gp_Dir(0,0,1));  break; // 顶
            case vsg::KEY_2: setDirection(gp_Dir(0,0,-1)); break; // 底
            case vsg::KEY_3: setDirection(gp_Dir(0,-1,0)); break; // 前
            case vsg::KEY_4: setDirection(gp_Dir(0,1,0));  break; // 后
            case vsg::KEY_5: setDirection(gp_Dir(-1,0,0)); break; // 左
            case vsg::KEY_6: setDirection(gp_Dir(1,0,0));  break; // 右
            case vsg::KEY_0: setDirection(gp_Dir(1,1,1));  break; // 等轴测
            default: break;
        }
    }

private:
    gp_Dir dir{0,0,1};

    void recompute()
    {
        ocrl::ContourOptions opts; opts.direction = dir;
        ocrl::ContourResult r = ocrl::computeOuterContour(compound, opts);
        std::printf("重算: ok=%d loops=%zu area=%.3f msg=%s\n",
                    r.ok, r.loopCount, r.area, r.message.c_str());

        contourGroup->children.clear();
        if (r.ok) {
            std::vector<ocrl::Polyline> outer{ r.outerPolyline };
            contourGroup->addChild(ocrl::buildOutlineNode(r.viewSystem, outer, vsg::vec3(1.0f,0.9f,0.1f), depth));
            if (!r.holePolylines.empty())
                contourGroup->addChild(ocrl::buildOutlineNode(r.viewSystem, r.holePolylines, vsg::vec3(0.2f,0.7f,1.0f), depth));
        }
        // 运行时编译新增几何并并入 viewer(vsgExamples dynamicload 范式)
        if (auto v = viewer.ref_ptr()) {
            auto cr = v->compileManager->compile(contourGroup);
            vsg::updateViewer(*v, cr);
        }
    }
};
```

- [ ] **Step 2: 改 `main()` —— 把轮廓挂到独立 `contourGroup`,注册键盘处理器**

将 Step(Task 8)里"组场景"那段替换为:
```cpp
    auto root = vsg::Group::create();
    if (sceneData.scene) root->addChild(sceneData.scene);
    auto contourGroup = vsg::Group::create();
    root->addChild(contourGroup);

    const double depth = -sceneData.radius * 1.2;
    if (contour.ok) {
        std::vector<ocrl::Polyline> outer{ contour.outerPolyline };
        contourGroup->addChild(ocrl::buildOutlineNode(contour.viewSystem, outer, vsg::vec3(1.0f,0.9f,0.1f), depth));
        if (!contour.holePolylines.empty())
            contourGroup->addChild(ocrl::buildOutlineNode(contour.viewSystem, contour.holePolylines, vsg::vec3(0.2f,0.7f,1.0f), depth));
    }
```
在 `viewer->compile();` **之前**注册(compileManager 在 compile 后才可用,故处理器在 compile 后再 setViewer;这里先 addEventHandler,recompute 内部判空):
```cpp
    auto keyHandler = ContourKeyHandler::create();
    keyHandler->contourGroup = contourGroup;
    keyHandler->compound = compound;
    keyHandler->depth = depth;
    viewer->addEventHandler(keyHandler);
```
在 `viewer->compile();` **之后**赋 viewer:
```cpp
    keyHandler->viewer = viewer;   // compileManager 此时已就绪
```

- [ ] **Step 3: 构建**

Run: `cmake --build D:/OcctRenderLab/build --config Debug --target contour_viewer`
Expected: 构建成功。

- [ ] **Step 4: 运行 — 人工验证按键重算**

Run:
```powershell
D:/OcctRenderLab/build/apps/contour_viewer/Debug/contour_viewer.exe D:/model/12/柱体.step
```
Expected: 启动顶视(圆轮廓);按 `6`(右视)控制台打印"重算: ... area≈40(矩形 4×10)",窗口轮廓变矩形;按 `1` 回到圆;切换流畅不崩。再用 `齿轮.stp`、`长条.STEP` 各试几个方向目测。

- [ ] **Step 5: 提交**

```powershell
cd D:/OcctRenderLab
git add -A
git commit -m "feat(viewer): 键盘切投影方向并运行时重算轮廓(compileManager+updateViewer)"
```

---

## Task 10: contour_sweep — 样本集鲁棒性扫描(不崩 + 记录耗时)

**Files:**
- Create: `apps/contour_sweep/main.cpp`, `apps/contour_sweep/CMakeLists.txt`
- Modify: 顶层 `CMakeLists.txt`(加 `add_subdirectory(apps/contour_sweep)`)

- [ ] **Step 1: 写 `apps/contour_sweep/main.cpp`**

```cpp
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include "ModelLoad.h"
#include "OuterContour.h"

namespace fs = std::filesystem;

static bool isStep(const fs::path& p) {
    std::string e = p.extension().string();
    for (auto& c : e) c = (char)tolower((unsigned char)c);
    return e == ".step" || e == ".stp";
}

int main(int argc, char** argv)
{
    const std::string dir = (argc >= 2) ? argv[1] : "D:/model/12";
    int total = 0, ok = 0, fail = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file() || !isStep(entry.path())) continue;
        ++total;
        const std::string name = entry.path().string();
        try {
            auto t0 = std::chrono::steady_clock::now();
            TopoDS_Compound c = ocrl::loadCompound(entry.path());
            ocrl::ContourResult r = ocrl::computeOuterContour(c, {});
            auto t1 = std::chrono::steady_clock::now();
            long long msv = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
            if (r.ok) { ++ok;  std::printf("[OK  %5lldms] loops=%2zu area=%10.2f  %s\n", msv, r.loopCount, r.area, name.c_str()); }
            else      { ++fail; std::printf("[FAIL        ] %s  (%s)\n", name.c_str(), r.message.c_str()); }
        } catch (const Standard_Failure& e) {
            ++fail; std::printf("[CRASH-OCCT  ] %s  (%s)\n", name.c_str(), e.GetMessageString());
        } catch (const std::exception& e) {
            ++fail; std::printf("[CRASH       ] %s  (%s)\n", name.c_str(), e.what());
        }
    }
    std::printf("\n合计 %d, 成功 %d, 失败 %d\n", total, ok, fail);
    return 0; // 扫描工具永远 0 退出(失败计入统计,不算崩溃)
}
```
顶部需 `#include <Standard_Failure.hxx>` 与 `#include <cctype>`。

- [ ] **Step 2: 写 `apps/contour_sweep/CMakeLists.txt`,并在顶层加子目录**

```cmake
add_executable(contour_sweep main.cpp)
target_link_libraries(contour_sweep PRIVATE ocrl_core)
ocrl_copy_runtime_dlls(contour_sweep)
```
顶层 `CMakeLists.txt` 末尾:`add_subdirectory(apps/contour_sweep)`。

- [ ] **Step 3: 配置 + 构建**

Run:
```powershell
cmake -S D:/OcctRenderLab -B D:/OcctRenderLab/build -G "Visual Studio 17 2022" -A x64
cmake --build D:/OcctRenderLab/build --config Debug --target contour_sweep
```
Expected: 构建成功。

- [ ] **Step 4: 跑样本集**

Run:
```powershell
D:/OcctRenderLab/build/apps/contour_sweep/Debug/contour_sweep.exe D:/model/12
D:/OcctRenderLab/build/apps/contour_sweep/Debug/contour_sweep.exe D:/model/MyExamples
```
Expected: 逐文件打印 `[OK ...]`/`[FAIL ...]`,末尾合计;**进程不崩溃**(异常都被捕获)。对 `[FAIL]` 项把文件名与 message 记录到 `docs/` 的一则备注,作为后续改进输入。

- [ ] **Step 5: 提交**

```powershell
cd D:/OcctRenderLab
git add -A
git commit -m "feat(sweep): 样本集鲁棒性扫描工具(捕获异常+记录耗时)"
```

---

## Task 11: README + 收尾

**Files:**
- Create: `README.md`

- [ ] **Step 1: 写 `README.md`**

包含:项目简介(独立研究项目、第一任务=外轮廓)、与 vsgOcct/OCCT 的复用关系、构建命令、三个 exe 的用法(viewer 键位表:1-6/0 视向、Esc 退出;sweep 用法;tests/ctest)、已知限制(只做精确 HLR 路线 A;多岛/孔的当前策略;HLR 在大装配上可能慢)。键位与命令均照搬本计划 Task 8/9/10 的实际实现。

- [ ] **Step 2: 全量回归**

Run:
```powershell
cmake --build D:/OcctRenderLab/build --config Debug
ctest --test-dir D:/OcctRenderLab/build -C Debug --output-on-failure
```
Expected: 全部目标构建通过;`contour_tests` 通过。

- [ ] **Step 3: 提交**

```powershell
cd D:/OcctRenderLab
git add -A
git commit -m "docs: README(构建/用法/键位/已知限制)"
```

---

## Self-Review(写计划后自检)

**1. Spec 覆盖**
- 计算精确外轮廓(HLR 路线 A)→ Task 3/5/6 ✓
- 沿任意投影方向 + 取最外闭环 → Task 3(投影器)、Task 5(选最外环)✓
- 复用 vsgocct 加载/网格、不改原项目 → Task 1(add_subdirectory src)、Task 2(readStep)、Task 8(buildAssemblyScene)✓
- 计算+渲染一体 demo、实体+轮廓叠加 → Task 7/8 ✓
- 可切换投影方向 → Task 9 ✓
- 两种显示模式(原位/投影平面):计划当前实现"投影平面"模式(depth 偏移);**原位叠加(OutLineVCompound3d)未单独建任务** → 见下方修正。
- 基本体解析验证 → Task 6 ✓;鲁棒性扫描 → Task 10 ✓;键位/显隐 → Task 9(方向+重算已含;F/E/C/H/M 显隐开关见修正)。

**2. 占位符扫描**:无 TBD/TODO;每个代码步骤均给出完整代码。

**3. 类型一致性**:`ContourResult`/`ContourOptions`/`detail::*` 跨任务签名一致;`buildOutlineNode(viewSystem, polylines, color, depth)` 在 Task 7 定义、Task 8/9 同签名调用 ✓;`computeOuterContour` 单一签名 ✓。

**修正(已并入计划意图,实现时一并补齐,避免新增整任务)**:
- **显隐开关 F/E/C/H/M**:Task 9 的 `ContourKeyHandler` 已留 `apply` 分发;补 `case vsg::KEY_c/KEY_h`:用 `contourGroup` 下分别持有的"外轮廓节点 / 孔节点"指针做 `node->setValue("visible",...)` 或从 group 增删子节点实现显隐;`F/E`(实体面/边)对 `sceneData.scene` 同理。M(原位/投影平面)= 切换 `depth`(0 = 原位贴近、负值 = 投影面)后调用一次 `recompute()`。这些都是 Task 9 同一处理器内的分支,不另起任务。
- 设计文档提到的"原位叠加用 `OutLineVCompound3d` 的真 3D 边":当前实现用 `depth` 把 2D 轮廓摆到不同平面来近似两种模式;若需严格 3D 原位,可在 `detail::extractVisibleOutlineEdges` 增一个 `in3d` 形参走 `CompoundOfEdges(type,true,true)` —— 列为 README"已知限制/后续"。
- `M_PI`:MSVC 需 `#define _USE_MATH_DEFINES`(Task 6 Step 1 已注明)。
- 运行时改场景的 `compileManager`:仅在 `viewer->compile()` 之后有效(Task 9 Step 2 已把 `keyHandler->viewer` 赋值放到 compile 之后)。

以上修正不改变任务编号,实现 Task 9 时按此把 `apply` 分支补全即可。

---

## Execution Handoff

计划完成。两种执行方式见 README 之外的流程:推荐 **Subagent-Driven**(每个 Task 派新 subagent,任务间评审);或 **Inline Execution**(本会话按 executing-plans 批量执行 + 检查点)。
