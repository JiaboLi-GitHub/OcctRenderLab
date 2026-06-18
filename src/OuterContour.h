#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

namespace ocrl {

struct ContourOptions {
    gp_Dir direction{0.0, 0.0, 1.0};   // 视线/投影方向(沿此方向看,正交)
    double connectTolerance = 1e-3;    // 拼环容差(mm 级机加工件需 >0.1µm)
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

// 把一条边离散成顺序点(>=2 个)。直线返回两端点;曲线按偏差采样。
Polyline discretizeEdge(const TopoDS_Edge& edge, double angDefl, double curvDefl);

struct AssembledLoops {
    std::vector<TopoDS_Wire> closedWires;   // 全部闭合环
    std::vector<double>      areas;          // 对应平面面积(>=0)
    int outerIndex = -1;                     // closedWires 中面积最大者下标
};
// 把 2D 边 Compound 连成闭合环,逐环算面积,标出最外环。
AssembledLoops assembleClosedLoops(const TopoDS_Compound& edges, double connectTol);

} // namespace detail

// 把视平面内 2D 点(Z≈0)映射回世界坐标,放到沿视向 depth 处的平面。
gp_Pnt mapViewToWorld(const gp_Ax2& viewSystem, const gp_Pnt& viewPoint, double depth);

} // namespace ocrl
