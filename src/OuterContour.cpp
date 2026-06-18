#include "OuterContour.h"

#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>

#include <BRep_Builder.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepGProp.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_HSequence.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <ShapeFix_Wire.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pnt.hxx>
#include <gp_XYZ.hxx>

#include <GeomAbs_CurveType.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static bool ocrlDebug() { return std::getenv("OCRL_DEBUG") != nullptr; }
static std::size_t ocrlCountEdges(const TopoDS_Shape& s) {
    std::size_t n = 0;
    for (TopExp_Explorer e(s, TopAbs_EDGE); e.More(); e.Next()) ++n;
    return n;
}

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
    if (ocrlDebug())
        std::printf("[HLR] outline=%zu sharp=%zu total=%zu\n",
                    ocrlCountEdges(outline), ocrlCountEdges(sharp), ocrlCountEdges(out));
    return out;
}

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
    if (ocrlDebug())
        std::printf("[ASM] rawEdges=%d wires=%d (tol=%g)\n",
                    edges->Length(), wires->Length(), connectTol);

    // 3) 逐环:修小缝隙 -> 闭合判定 -> 建面算面积
    double maxArea = -1.0;
    for (int i = 1; i <= wires->Length(); ++i)
    {
        TopoDS_Wire wire = TopoDS::Wire(wires->Value(i));
        const std::size_t nbBefore = ocrlCountEdges(wire);

        ShapeFix_Wire fix;
        fix.Load(wire);
        fix.SetPrecision(connectTol);
        fix.FixConnected();
        fix.FixClosed();
        fix.Perform();
        wire = fix.Wire();

        // 不用 TopoDS_Shape::Closed() 拓扑标志位(ConnectEdgesToWires/ShapeFix
        // 常不为多边 wire 设置它,导致只有单边圆能通过)。改用「能否建平面」作为
        // 真正的闭合判据:平面面只能由几何闭合的 wire 构成。
        BRepBuilderAPI_MakeFace mkFace(wire, Standard_True); // OnlyPlane
        if (ocrlDebug())
            std::printf("[ASM]   wire#%d edges=%zu faceDone=%d\n",
                        i, nbBefore, mkFace.IsDone() ? 1 : 0);
        if (!mkFace.IsDone())
            continue;

        GProp_GProps props;
        BRepGProp::SurfaceProperties(mkFace.Face(), props);
        const double area = std::fabs(props.Mass()); // 顺/逆时针 wire 的 Mass 符号会反,取绝对值
        if (ocrlDebug())
            std::printf("[ASM]   wire#%d area=%.3f\n", i, area);

        result.closedWires.push_back(wire);
        result.areas.push_back(area);
        if (area > maxArea)
        {
            maxArea = area;
            result.outerIndex = static_cast<int>(result.closedWires.size()) - 1;
        }
    }
    return result;
}

} // namespace detail

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
        if (edges.IsNull()) { r.message = "HLR extract empty"; return r; }

        detail::AssembledLoops loops = detail::assembleClosedLoops(edges, opts.connectTolerance);
        if (loops.outerIndex < 0) { r.message = "no closed outer loop"; return r; }

        r.outerWire = loops.closedWires[loops.outerIndex];
        r.area      = loops.areas[loops.outerIndex];
        r.loopCount = loops.closedWires.size();
        r.straightEdgeCount = countStraightEdges(r.outerWire);

        for (std::size_t i = 0; i < loops.closedWires.size(); ++i)
            if (static_cast<int>(i) != loops.outerIndex)
                r.holeWires.push_back(loops.closedWires[i]);

        r.outerPolyline = wireToPolyline(r.outerWire, opts.angularDeflection, opts.curvatureDeflection);
        std::vector<Polyline> all{ r.outerPolyline };
        for (const auto& hw : r.holeWires) {
            Polyline hp = wireToPolyline(hw, opts.angularDeflection, opts.curvatureDeflection);
            r.holePolylines.push_back(hp);
            all.push_back(hp);
        }
        polylineBBox2d(all, r.bboxMinX, r.bboxMinY, r.bboxMaxX, r.bboxMaxY);
        r.ok = true;
    } catch (const Standard_Failure& e) {
        r.ok = false; r.message = std::string("OCCT exception: ") + e.GetMessageString();
    } catch (const std::exception& e) {
        r.ok = false; r.message = std::string("exception: ") + e.what();
    }
    return r;
}

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
