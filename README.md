# OcctRenderLab

研究 OCCT(Open CASCADE Technology)与渲染相关题目的实验室。
**第一个题目:模型外轮廓(精确投影剪影)+ VSG 渲染。**

仓库分成**两个可独立编译的项目**:

- **`contour/`** —— 外轮廓**算法库**(`occtcontour`),**只依赖 OCCT**,可独立编译、安装,被其他项目
  通过 `find_package(occtcontour)` 或 `add_subdirectory(contour)` 单独使用。
- **`render/`** —— VSG 可视化(`contour_viewer`),依赖 `contour` + vsg + vsgocct。

给定一个 STEP 模型和投影方向,用 OCCT 精确算出模型沿该方向投影的**剪影外轮廓**(最外闭环 + 内孔),
等价于 **Fusion 360「2D 轮廓加工」** 选实体时的那种外形轮廓。

## 项目结构

```
OcctRenderLab/                      ← 仓库根 = 超级构建(super-build)
├─ CMakeLists.txt                   add_subdirectory(contour) [+ render]
├─ contour/                         ① 算法库(纯 OCCT)
│  ├─ include/occtcontour/{OuterContour.h, ModelLoad.h}
│  ├─ src/{OuterContour.cpp, ModelLoad.cpp}
│  ├─ apps/{contour_sweep, contour_svg}   纯 OCCT 工具
│  ├─ tests/(+ tests/data 样本)
│  ├─ examples/consumer_smoke/             find_package 最小消费示例
│  └─ cmake/occtcontourConfig.cmake.in
└─ render/                          ② 渲染(依赖 ① + vsg + vsgocct)
   ├─ include/ocrlrender/OutlineRender.h
   ├─ src/OutlineRender.cpp
   └─ apps/contour_viewer/main.cpp
```

依赖:`contour` ← `render`。算法库 C++ 命名空间 `ocrl`,头文件 `<occtcontour/...>`。

## 方法(精确 B-rep 投影阴影区域并集)

```
STEP ─loadCompound(STEPControl_Reader)──► TopoDS_Compound
                                          ├─ buildAssemblyScene(vsgocct,渲染侧) ─► VSG 实体
                                          └─ computeOuterContour(算法侧,纯 OCCT)
                                               HLRBRep_Algo 正交投影 → 可见+隐藏的 剪影+锐边(2D@Z=0)
                                               → BRepLib::BuildCurves3d 补 3D 曲线
                                               → 视平面 base 面用这些边 Splitter 切子面
                                               → 逐子面取内部点、沿视向投射线 IntCurvesFace 判是否击中实体
                                                 (穿过通孔的线不命中 ⇒ 自动成为内孔)
                                               → Fuse 阴影子面 + UnifySameDomain
                                               → BRepTools::OuterWire 取外环、其余为内孔
                                               → BRepTools_WireExplorer 按连通顺序离散
                                          ─► VSG 线节点(反向 Z 管线)
```

为什么不用「HLR 边直接拼环」:复杂/带圆角件投影出的边不是连续闭链(圆角双线、形状突变处分叉),
`ConnectEdgesToWires`+取最大面积会把外轮廓**拆裂**、把刀身这类大段**误判成内孔**。改用「视平面切子面 →
射线判定每片是否在实体阴影内 → 并集」是拓扑稳健的精确解:球/圆角/凹形/通孔都正确,只产生**一条完整外环**。
回归测试 `test_knife_outer_complete` 守护这一点(外环须横跨整把刀 198mm)。

## 构建

> **必须用 `Visual Studio 18 2026` 生成器(MSVC 14.50.35717)**,与 vsg/vsgOcct 一致。
> 用 VS2022(14.44)链接 vsg 着色器编译器(glslang/SPIRV-Tools)会报 `__std_*` 未解析符号
> —— 14.44 的 STL 缺新版向量化算法符号。(只编算法库时不涉及 vsg,VS2022 亦可。)

**完整(算法+渲染)**:
```powershell
cmake -S D:/OcctRenderLab -B D:/OcctRenderLab/build -G "Visual Studio 18 2026" -A x64
cmake --build D:/OcctRenderLab/build --config Debug
```

**只编算法库**(不碰 vsg/vsgocct):
```powershell
cmake -S D:/OcctRenderLab -B build_algo -G "Visual Studio 18 2026" -A x64 -DOCRL_BUILD_RENDER=OFF
cmake --build build_algo --config Debug
```

依赖:OCCT 构建树 `D:/OCCT/build`;vsg `C:/Program Files (x86)/vsg`;vsgOcct `D:/vsgOcct`。
各 CMake 已强制单一 Debug 配置(OCCT/Vulkan 只提供 Debug),并自动把运行期 DLL 拷到 exe 旁。

## 在其他项目中单独使用算法

**方式一:install + find_package**
```powershell
cmake --install build_algo --config Debug --prefix <prefix>
```
```cmake
find_package(occtcontour REQUIRED)        # 消费方 CMake;-DCMAKE_PREFIX_PATH=<prefix>
target_link_libraries(myapp PRIVATE occtcontour::occtcontour)
```
**方式二:源码引入**
```cmake
add_subdirectory(<path>/OcctRenderLab/contour)
target_link_libraries(myapp PRIVATE occtcontour::occtcontour)
```
用法(见 `contour/examples/consumer_smoke/main.cpp`):
```cpp
#include <occtcontour/ModelLoad.h>
#include <occtcontour/OuterContour.h>
auto shape = ocrl::loadCompound("model.step");
ocrl::ContourOptions o; o.direction = gp_Dir(0,0,1);
ocrl::ContourResult r = ocrl::computeOuterContour(shape, o);  // r.outerWire / r.holeWires / r.outerPolyline / r.area
```

## 可执行体

> VS 生成器输出在子目录:`build/contour/Debug/`(算法工具/测试)、`build/render/Debug/`(viewer)。

### contour_viewer(渲染侧)
```powershell
build\render\Debug\contour_viewer.exe <model.step> [dx dy dz] [--frames N] [--cycle] [--loops all|outer|inner]
```
键位:`1`-`6`/`0` 视向、`7`/`8`/`9` 全部/外/内回路、`F`/`C` 显隐实体/轮廓、`M` 投影面↔原位、鼠标轨道、`Esc` 退出。

### contour_sweep / contour_svg(算法侧,纯 OCCT)
```powershell
build\contour\Debug\contour_sweep.exe [目录]                 # 批量算外轮廓,记录 ok/fail+耗时
build\contour\Debug\contour_svg.exe <model.step> [dx dy dz] [--defl v] -o out.svg   # 导出轮廓 SVG
```

### 测试
```powershell
ctest --test-dir build -C Debug --output-on-failure
```

## 验证结果

- `contour_tests`:全过(柱体顶视 4π;球体 π/4;薄板/长条/骷髅匕首 外环横跨实体投影框)。
- `contour_sweep`:`D:/model/12`(23)+ `MyExamples`(11)共 **34/34** 成功、0 崩溃。
- 算法库**零 vsg**:`-DOCRL_BUILD_RENDER=OFF` 构建不查找 vsg/Qt,产物旁无 vsg DLL。
- `find_package(occtcontour)` 外部消费:`consumer_smoke` 编译运行 `ok=1`。
- `contour_viewer --frames`:GPU 管线跑通;`--cycle` 运行时切向/切回路无崩溃。

## 已知限制 / 后续

- 「原位 / 投影平面」两种显示当前用 `depth` 偏移近似;严格 3D 原位剪影可用 `OutLineVCompound3d()`。
- 多体目前只返回面积最大那块的外环;「每块各取外环」待扩展。
- 区域并集精确但较重:子面越多越慢,复杂件(齿轮)Debug 下约 7s(精度优先;仅 Debug 配置)。
- 交互窗口需要桌面会话;非交互 shell 启动可能立即退出(用 `--frames` 无人值守验证)。
- 已可导出 SVG(`contour_svg`);DXF/STEP 导出尚未实现。

## License

MIT
