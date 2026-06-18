# OcctRenderLab

研究 OCCT(Open CASCADE Technology)与渲染相关题目的独立实验室。
**第一个题目:模型外轮廓(精确 HLR)+ VSG 渲染。**

给定一个 STEP 模型和一个投影方向,用 OCCT 的隐藏线消除(HLR)精确算出模型沿该方向投影的
**剪影外轮廓**(最外层闭环 + 内孔),并在 VulkanSceneGraph(VSG)视图里把实体与轮廓一起显示,
可交互切换投影方向。等价于 **Fusion 360「2D 轮廓加工」** 选实体时所用的那种外形轮廓。

## 与其他项目的关系(只复用,不修改)

- **`D:\OCCT`** —— OCCT 8.0.0 构建树(`D:\OCCT\build`,Debug)。本项目链接其 `TKHLR` 等工具包。
- **`D:\vsgOcct`** —— VSG+OCCT 桥接库。本项目通过 `add_subdirectory(D:/vsgOcct/src)` 复用其
  `cad::readStep`(STEP 读取)与 `scene::buildAssemblyScene`(实体场景),**不改动其源码**。

## 方法(算法主线:路线 A,精确 HLR)

```
STEP ─readStep(复用)─► 装配树 ─拍平─► TopoDS_Compound
                                          ├─ buildAssemblyScene(复用) ─► VSG 实体
                                          └─ computeOuterContour(本项目)
                                               HLRBRep_Algo 正交投影
                                               → OutLineVCompound(剪影边) + VCompound(锐边)
                                               → ConnectEdgesToWires 拼环
                                               → 选「能建平面 + 面积最大」的闭环为外轮廓
                                               → BRepGProp 算面积、离散为折线
                                          ─► VSG 线节点(反向 Z 管线)
```

关键点:剪影边(`OutLineVCompound`)捕捉曲面与视向相切处的轮廓(如圆柱侧母线),锐边
(`VCompound`)补齐棱柱/平面的边界 —— 两者合并才得到完整外形。闭合判据用
`BRepBuilderAPI_MakeFace.IsDone()`(平面只能由几何闭合的 wire 构成),而非拓扑 `Closed()` 标志位。

## 构建

> **必须用 `Visual Studio 18 2026` 生成器(MSVC 14.50.35717)**,与 vsg / vsgOcct 一致。
> 用 VS2022(14.44)会在链接 vsg 着色器编译器(glslang/SPIRV-Tools)时报 `__std_*` 未解析符号
> —— 那是 14.44 的 STL 缺少新版向量化算法符号所致。

```powershell
cmake -S D:/OcctRenderLab -B D:/OcctRenderLab/build -G "Visual Studio 18 2026" -A x64
cmake --build D:/OcctRenderLab/build --config Debug
```

依赖:OCCT 构建树在 `D:/OCCT/build`,vsg 在 `C:/Program Files (x86)/vsg`,vsgOcct 在 `D:/vsgOcct`。
顶层 CMake 已强制单一 Debug 配置(OCCT/Vulkan 只提供 Debug 产物),并自动把运行期 DLL 拷到 exe 旁。

## 可执行体

### 1. `contour_viewer` —— 计算 + 渲染 demo

```powershell
build\apps\contour_viewer\Debug\contour_viewer.exe <model.step> [dx dy dz] [--frames N] [--cycle]
# 例:
build\apps\contour_viewer\Debug\contour_viewer.exe D:\model\12\柱体.step
build\apps\contour_viewer\Debug\contour_viewer.exe D:\model\12\柱体.step 1 0 0   # 侧视
```

- 可选 `dx dy dz`:初始投影方向(默认 `0 0 1` 顶视)。
- `--frames N`:渲染 N 帧后退出(自动化验证用,不阻塞)。
- `--cycle`:运行中自动切几次方向,验证运行时重算路径。

键位:

| 键 | 作用 |
|---|---|
| `1` `2` `3` `4` `5` `6` | 顶 / 底 / 前 / 后 / 左 / 右 视向(切向即重算轮廓) |
| `0` | 等轴测方向 |
| `F` | 显隐实体 |
| `C` | 显隐外轮廓 |
| `M` | 切换「投影平面」/「原位」显示模式 |
| 鼠标 | trackball 轨道视角 |
| `Esc` | 退出 |

外轮廓为亮黄色,内孔为青色;切换投影方向时控制台打印 `recompute: ok/loops/area`。

### 2. `contour_sweep` —— 鲁棒性扫描

```powershell
build\apps\contour_sweep\Debug\contour_sweep.exe [目录]   # 默认 D:/model/12
```

遍历目录下所有 `.step`/`.stp`,逐个算外轮廓,打印 `[OK 耗时] loops/area 文件名` 或失败原因,
末尾给出合计。异常被捕获,进程恒以 0 退出。

### 3. `contour_tests` —— 解析验证

```powershell
ctest --test-dir D:/OcctRenderLab/build -C Debug --output-on-failure
```

用自包含断言验证基本体(测试模型在 `tests/data/`,从 `D:\model\12` 复制为 ASCII 名):
- **柱体**:顶视外轮廓面积 = 4π(精确);侧视 = 4×10 矩形(aspect 2.5)。
- **球体**:任意视向 = 圆(面积/包围盒 ≈ π/4,无直边)。
- **薄板 / 长条**:严谨不变量 —— 轮廓 2D 外接框 == 实体投影 3D 外接框(板 100×99.5,条 26×113)。

## 验证结果

- `contour_tests`:全部通过(柱体顶视面积 12.5664 = 4π 精确)。
- `contour_sweep`:`D:/model/12`(23)+ `MyExamples`(11)共 **34/34 成功,0 崩溃**,耗时 6–757ms。
- `contour_viewer --frames`:GPU 管线(着色器编译 + 提交 + 呈现)跑通;`--cycle` 验证运行时重算无崩溃。

## 已知限制 / 后续

- 只实现**路线 A(精确 HLR)**;网格 HLR(`HLRBRep_PolyAlgo`)与网格投影 2D 布尔并集列为后续。
- 「原位 / 投影平面」两种显示当前用 `depth` 偏移近似;严格 3D 原位剪影可用
  `OutLineVCompound3d()` / `CompoundOfEdges(type,true,/*In3d=*/true)`。
- 多个分离投影「岛」当前取全局面积最大的闭环为外轮廓;完整的「每岛各取外环 + 嵌套孔分类」待完善。
- HLR 是精确算法,超大装配可能较慢(本项目最慢样例齿轮 757ms)。
- 交互窗口需要桌面会话;从非交互 shell 启动可能立即退出(用 `--frames` 做无人值守验证)。
- 轮廓导出 DXF/SVG/STEP 尚未实现。

## 目录

```
src/ModelLoad.{h,cpp}              读 STEP + 拍平装配为 TopoDS_Compound
src/OuterContour.{h,cpp}           核心:HLR → 拼环 → 选最外环 → 度量
apps/contour_viewer/               VSG 显示 demo(OutlineRender 线管线 + main)
apps/contour_sweep/                鲁棒性扫描
tests/                             解析验证 + tests/data 样本
docs/specs, docs/plans             设计文档与实现计划
```

## License

MIT
