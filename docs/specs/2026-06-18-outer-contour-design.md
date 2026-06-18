# 设计文档:OCCT 模型外轮廓(精确 HLR)+ VSG 渲染 Demo

- 日期:2026-06-18
- 项目:`OcctRenderLab`(研究 OCCT 与渲染的实验室)
- 任务编号:第 1 个任务 —— 模型外轮廓
- 状态:已通过 brainstorming 评审,待实现

---

## 1. 背景与目标

`OcctRenderLab` 是一个**独立**的研究项目,用来逐项研究 OCCT(Open CASCADE Technology)
与渲染相关的题目。第一个题目是**模型外轮廓**。

经讨论确认,这里的"外轮廓"等价于 **Autodesk Fusion 360「2D 轮廓加工(2D Contour)」**
所用的轮廓:把实体**沿刀轴(投影方向)正交投影到一个平面**,得到的**剪影(silhouette)
轮廓**,再取其**最外层闭环**。它包含曲面与投影方向相切处的剪影边,因此**视向相关**,
是**精确的 2D 几何曲线**,而不是:
- 按夹角阈值提取的真实棱边线框(feature edge overlay),也不是
- 屏幕空间描边特效(screen-space outline)。

在 OCCT 中,这正是 **HLRBRep(隐藏线消除)** 模块的能力。

### 1.1 第一个交付物

一个**计算 + 渲染一体的 demo**:用 OCCT 精确算出模型外轮廓,并在 VSG 视图中把
**实体与外轮廓一起显示**,支持切换投影方向。

### 1.2 与现有项目的关系

- **不修改** `D:\OCCT`(OCCT 源码,已编译于 `D:\OCCT\build`,含 `TKHLR`)。
- **不修改** `D:\vsgOcct`(VSG+OCCT 桥接库,已编译出 `vsgocct.lib`)。
- 本项目**独立**建立,但通过 CMake **链接复用** vsgOcct 的加载与三角化代码,
  避免重复造轮子。

### 1.3 算法主线

只做**路线 A:精确 HLR(`HLRBRep_Algo`)**。
不做网格 HLR(路线 B)和网格投影并集(路线 C),它们列入未来可选实验。

---

## 2. 复用的既有能力(事实已核实)

- `vsgocct::cad::readStep(path)` → 返回 `AssemblyData{ roots: vector<ShapeNode> }`,
  每个 `ShapeNode` 含 `TopoDS_Shape shape`、`TopLoc_Location location`、`children`。
  → HLR 需要的精确 B-Rep 可取得。
- `vsgocct::mesh::triangulate(shape, MeshOptions)` → 返回 `MeshResult`
  (面/边/点位置、法线、`boundsMin/boundsMax`)。→ 用于渲染实体与相机定位。
- OCCT 已编译:`D:\OCCT\build` 含 `TKHLR`(HLR)、`TKMesh` 等。
- vsgocct 已编译:`D:\vsgOcct\build\src\vsgocct\Debug\vsgocct.lib`,依赖
  vsg(`C:\Program Files (x86)\vsg`)与 Qt6(`C:\Qt\6.10.1\msvc2022_64`)。
- 测试素材:`D:\model\12\`(`柱体.step`、`球体.step`、`板.stp`、`长条.STEP`、
  `齿轮.stp`、`轴套.STEP` 等)与 `D:\model\MyExamples\`、`D:\OCCT\data\step\`。

---

## 3. 目录结构

```
D:\OcctRenderLab\
├─ CMakeLists.txt                 # 顶层:复用 vsgocct 目标 + 链接 OCCT(含 TKHLR)+ vsg
├─ README.md / README_EN.md
├─ docs\specs\2026-06-18-outer-contour-design.md   # 本文档
├─ src\
│   ├─ ModelLoad.{h,cpp}          # 复用 cad::readStep;把装配树拍平为单个 TopoDS_Compound(应用 location)
│   └─ OuterContour.{h,cpp}       # ★核心:HLR 投影 → 抽剪影边 → 拼环 → 选最外环
├─ apps\contour_viewer\main.cpp   # ★交付物:计算+渲染一体 demo(纯 vsg::Viewer)
└─ tests\contour_tests.cpp        # 基本体外轮廓的解析验证(可选用 gtest 或最小断言)
```

未来新实验只在 `src/` 加模块、`apps/` 加程序,互不干扰。

---

## 4. 整体数据流

```
STEP ──readStep(复用)──► AssemblyData ──ModelLoad 拍平──► TopoDS_Compound
                                                            ├─ triangulate(复用) ─► 实体网格 ─► VSG 实体节点
                                                            └─ OuterContour(新)  ─► 外轮廓 wire ─► VSG 线节点
                                                                                              ▼
                                                       vsg::Viewer(实体 + 轮廓叠加;可切换投影方向并重算)
```

单次读盘;同一份精确 B-Rep 同时喂给"渲染网格"和"HLR 轮廓"。

---

## 5. 模块设计

### 5.1 `ModelLoad`(加载与拍平)

职责:调用 `vsgocct::cad::readStep` 读 STEP,递归遍历装配树,把每个 `Part` 的
`shape` 按其累积 `location` 变换后塞进一个 `TopoDS_Compound`,返回该 compound。

- 输入:STEP 文件路径。
- 输出:`TopoDS_Compound`(已应用 location 的世界坐标几何)。
- 说明:HLR 与三角化都基于这个 compound,保证两者坐标一致。

### 5.2 `OuterContour`(研究核心)

职责:对给定形状沿给定方向做精确 HLR,提取外轮廓闭环。

接口(草案):
```cpp
namespace ocrl {

struct ContourOptions {
    gp_Dir direction{0,0,1};   // 投影方向(刀轴);默认沿 +Z 看(顶视)
    gp_Dir up{0,1,0};          // 视图 up,用于构造视坐标系
    double connectTolerance = 1e-4; // 拼环容差
    bool includeHoles = true;       // 是否同时输出内孔环
};

struct ContourResult {
    TopoDS_Wire outerWire;                 // 最外层闭环(精确曲线)
    std::vector<TopoDS_Wire> holeWires;    // 内孔环(可选)
    std::vector<std::vector<gp_Pnt>> outerPolyline;  // 离散折线(渲染/测试用)
    std::vector<std::vector<gp_Pnt>> holePolylines;
    double area = 0.0;       // 外环在投影平面的面积
    Bnd_Box2d planarBounds;  // 投影平面包围盒
    bool ok = false;
    std::string message;
};

ContourResult computeOuterContour(const TopoDS_Shape& shape,
                                  const ContourOptions& opts);
} // namespace ocrl
```

算法步骤:
1. 由 `direction` + `up` 构造视坐标系 `gp_Ax2`,据此构造 `HLRAlgo_Projector`
   (**正交/平行**投影,对应 CAM 沿刀轴投影)。
2. `HLRBRep_Algo`:`Add(shape)` → `Projector(projector)` → `Update()` → `Hide()`。
3. `HLRBRep_HLRToShape`:取
   - `OutLineVCompound()`:可见**剪影/外形边**(含曲面相切剪影);
   - `VCompound()`:可见**棱边**(补齐平直边界)。
4. 将上述边收集后用
   `ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, connectTolerance, false, wires)`
   拼成闭合 wire;必要时对未闭合 wire 用 `ShapeFix_Wire` 修补。
5. **选最外环**:在投影平面内对每个闭合 wire 算包围盒/有向面积,按**包含关系**
   判定外环与内孔环;若投影出现多个分离"岛",各岛各取其外环
   (第一版以"全局最外 + 其内部的孔"为主,多岛在 `message` 中记录并尽量全部输出)。
6. 输出:精确 `outerWire`(+ `holeWires`)、离散 `outerPolyline`、`area`、`planarBounds`。

已知风险与对策:
- **HLR 边有微小缝隙** → 拼环带 `connectTolerance`,必要时 `ShapeFix_Wire`。
- **外/内环判定** → 用投影平面包围盒包含测试 + 有向面积符号。
- **坐标系映射** → HLR 结果位于投影平面;渲染时按视坐标系映射回世界坐标。
- **HLR 在大/脏装配上可能慢或异常** → 第一版以中小模型为主;捕获异常并在
  `ContourResult.message` 中报告,不崩溃。

### 5.3 `contour_viewer`(计算 + 渲染 demo)

职责:加载模型 → 算外轮廓 → 用纯 `vsg::Viewer` 把实体与轮廓一起显示。

- **实体渲染**:复用 `mesh::triangulate` 的 `MeshResult` 构建 VSG 面几何;
  相机由 `boundsMin/boundsMax` 自动定位(trackball 可轨道旋转)。
- **轮廓渲染**:把 `outerPolyline`/`holePolylines` 构建为 `LINE_LIST` 几何;
  外轮廓用醒目颜色(如亮黄),内孔环另色。
- **两种显示模式**(`M` 键切换):
  1. **原位叠加**:剪影边贴在 3D 模型上(在其投影位置)。
  2. **投影平面**:把轮廓拍平到一个平面上(沿投影方向偏移到模型范围外),
     像投影/2D 图——最能体现"2D 轮廓"。
- **键盘交互**:
  - `1`-`6`:六视向预设(顶/底/前/后/左/右),`0`:等轴测;切换即**重算轮廓**。
  - `F`/`E`/`C`/`H`:切换 实体面 / 模型棱边 / 外轮廓 / 内孔环 显示。
  - `M`:切换原位叠加 / 投影平面模式。
  - 鼠标:trackball 轨道视角。
- **控制台输出**:点/线/三角计数、外环面积、各阶段耗时(读取/三角化/HLR)。

---

## 6. 构建与依赖

- 顶层 `CMakeLists.txt`:
  - `set(VSGOCCT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)` 后
    `add_subdirectory(D:/vsgOcct ${CMAKE_BINARY_DIR}/_vsgOcct EXCLUDE_FROM_ALL)`,
    复用 `vsgocct` 目标并继承其对 OCCT/vsg 的查找(不碰 vsgOcct 源码、不需要 Qt)。
  - 本项目 target 额外链接 HLR 相关 OCCT 工具包:
    `TKHLR TKTopAlgo TKShHealing TKBRep TKGeomBase TKG3d TKG2d TKMath TKernel TKMesh`
    (随编译报错增删,以实际 HLR/拼环/修补所需为准)。
  - C++17,MSVC 2022,配置对齐 **Debug**(现有 `vsgocct.lib` 为 Debug)。
- 运行:
  `build\apps\contour_viewer\Debug\contour_viewer.exe D:\model\12\柱体.step`

---

## 7. 测试与成功标准

### 7.1 解析验证(`tests/contour_tests.cpp`)
对外轮廓可解析预测的基本体断言(按容差):
- `柱体.step`:顶视 → 圆(面积 ≈ πr²);侧视 → 矩形。
- `球体.step`:任意视向 → 圆。
- `板.stp` / `长条.STEP`:矩形(面积 ≈ 宽×高)。
断言项:闭合性、外环数量、面积、包围盒。

### 7.2 鲁棒性扫描
跑遍 `D:\model\12\` + `D:\model\MyExamples\`,记录每个文件的 成功/失败 + 耗时,
**不崩溃**(异常被捕获并记录)。

### 7.3 目测
`齿轮.stp`、`骷髅匕首(Skeleton+Knife).STEP` 等复杂件在 viewer 中目测合理。

### 7.4 完成定义(Definition of Done)
1. 给定 STEP + 投影方向,输出精确外轮廓 `TopoDS_Wire`。
2. viewer 显示 实体 + 轮廓叠加;`F/E/C/H/M` 开关有效;≥6 视向预设可切换并重算。
3. 7.1 基本体解析检查全部通过(容差内)。
4. 7.2 样本集扫描不崩溃,并记录耗时。

---

## 8. 明确不做(YAGNI)

以下列为未来可选实验,本任务不实现:
- 路线 B(网格 HLR `HLRBRep_PolyAlgo`)与路线 C(网格投影 + 2D 布尔并集)。
- 外轮廓导出 DXF/SVG/STEP(可作为后续 stretch)。
- 装配树/颜色/语义、稳定 ID 系统。
- 透视投影、刀具半径补偿/偏置。
- 多线程、headless 模式、性能基线体系。

---

## 9. 验收后下一步

本设计经用户复核后,进入 `writing-plans`,产出分步实现计划(TDD:先为
`OuterContour` 的基本体解析验证写测试,再实现)。
