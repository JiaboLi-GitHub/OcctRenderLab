#include "OuterContour.h"

#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <HLRBRep_TypeOfResultingEdge.hxx>

#include <BRep_Builder.hxx>
#include <BRepLib.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidExplorer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Bnd_Box.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Curve.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_CurveType.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <NCollection_List.hxx>
#include <Precision.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <ShapeFix_Wire.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax3.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_XYZ.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static bool ocrlDebug() { return std::getenv("OCRL_DEBUG") != nullptr; }

namespace ocrl {
namespace detail {

// ===== 低层 HLR 抽边(供测试/复用):仅「可见」剪影+锐边,2D 投影平面 =====
TopoDS_Compound extractVisibleOutlineEdges(const TopoDS_Shape& shape,
                                           const gp_Dir& direction,
                                           gp_Ax2& viewSystem)
{
    viewSystem = gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), direction);

    Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
    algo->Add(shape, 0);
    algo->Projector(HLRAlgo_Projector(viewSystem));
    algo->Update();
    algo->Hide();

    HLRBRep_HLRToShape toShape(algo);
    const TopoDS_Shape outline = toShape.OutLineVCompound();
    const TopoDS_Shape sharp   = toShape.VCompound();

    TopoDS_Compound out;
    BRep_Builder builder;
    builder.MakeCompound(out);
    if (!outline.IsNull()) builder.Add(out, outline);
    if (!sharp.IsNull())   builder.Add(out, sharp);
    return out;
}

Polyline discretizeEdge(const TopoDS_Edge& edge, double angDefl, double curvDefl)
{
    Polyline pts;
    BRepAdaptor_Curve curve(edge);
    GCPnts_TangentialDeflection disc(curve, angDefl, curvDefl);
    const int n = disc.NbPoints();
    pts.reserve(static_cast<std::size_t>(n > 0 ? n : 0));
    for (int i = 1; i <= n; ++i)
        pts.push_back(disc.Value(i));
    return pts;
}

// 旧的「边拼环」法,保留供低层测试;主入口已不再使用(复杂件会拆裂)。
AssembledLoops assembleClosedLoops(const TopoDS_Compound& edgesCompound, double connectTol)
{
    AssembledLoops result;
    opencascade::handle<NCollection_HSequence<TopoDS_Shape>> edges =
        new NCollection_HSequence<TopoDS_Shape>();
    for (TopExp_Explorer e(edgesCompound, TopAbs_EDGE); e.More(); e.Next())
        edges->Append(e.Current());
    if (edges->IsEmpty())
        return result;

    opencascade::handle<NCollection_HSequence<TopoDS_Shape>> wires =
        new NCollection_HSequence<TopoDS_Shape>();
    ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, connectTol, Standard_False, wires);
    if (wires.IsNull() || wires->IsEmpty())
        return result;

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

        BRepBuilderAPI_MakeFace mkFace(wire, Standard_True);
        if (!mkFace.IsDone())
            continue;
        GProp_GProps props;
        BRepGProp::SurfaceProperties(mkFace.Face(), props);
        const double area = std::fabs(props.Mass());
        result.closedWires.push_back(wire);
        result.areas.push_back(area);
        if (area > maxArea) { maxArea = area; result.outerIndex = int(result.closedWires.size()) - 1; }
    }
    return result;
}

} // namespace detail

// ===================== 新主算法:投影阴影区域并集(精确) =====================
namespace {

// HLR -> 全部分割边(可见+隐藏 的 outline + sharp),2D@全局 Z=0 平面。
TopoDS_Compound collectPartitionEdges(const TopoDS_Shape& shape, const gp_Ax2& viewCS)
{
    Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
    algo->Add(shape, 0);
    algo->Projector(HLRAlgo_Projector(viewCS));
    algo->Update();
    algo->Hide();
    HLRBRep_HLRToShape hls(algo);

    TopoDS_Compound out;
    BRep_Builder bb;
    bb.MakeCompound(out);
    auto addAll = [&](const TopoDS_Shape& s) {
        if (s.IsNull()) return;
        for (TopExp_Explorer e(s, TopAbs_EDGE); e.More(); e.Next()) bb.Add(out, e.Current());
    };
    addAll(hls.CompoundOfEdges(HLRBRep_OutLine, true,  false)); // 可见剪影
    addAll(hls.CompoundOfEdges(HLRBRep_OutLine, false, false)); // 隐藏剪影(内孔边界)
    addAll(hls.CompoundOfEdges(HLRBRep_Sharp,   true,  false)); // 可见锐边
    addAll(hls.CompoundOfEdges(HLRBRep_Sharp,   false, false)); // 隐藏锐边
    return out;
}

std::size_t countEdges(const TopoDS_Shape& s)
{
    std::size_t n = 0;
    for (TopExp_Explorer e(s, TopAbs_EDGE); e.More(); e.Next()) ++n;
    return n;
}

// 按「连通顺序」(WireExplorer)离散 wire,处理边朝向与接缝去重。
Polyline wireToPolylineOrdered(const TopoDS_Wire& wire, double angDefl, double curvDefl)
{
    Polyline pts;
    if (wire.IsNull()) return pts;
    for (BRepTools_WireExplorer wex(wire); wex.More(); wex.Next()) {
        BRepAdaptor_Curve curve(wex.Current());
        GCPnts_TangentialDeflection disc(curve, angDefl, curvDefl);
        std::vector<gp_Pnt> ep;
        for (int i = 1; i <= disc.NbPoints(); ++i) ep.push_back(disc.Value(i));
        if (wex.Orientation() == TopAbs_REVERSED) std::reverse(ep.begin(), ep.end());
        std::size_t start = (!pts.empty() && !ep.empty() && pts.back().IsEqual(ep.front(), 1e-6)) ? 1 : 0;
        for (std::size_t i = start; i < ep.size(); ++i) pts.push_back(ep[i]);
    }
    return pts;
}

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

} // namespace

gp_Pnt mapViewToWorld(const gp_Ax2& vs, const gp_Pnt& p, double depth)
{
    const gp_XYZ o = vs.Location().XYZ();
    const gp_XYZ x = vs.XDirection().XYZ();
    const gp_XYZ y = vs.YDirection().XYZ();
    const gp_XYZ z = vs.Direction().XYZ();
    const gp_XYZ w = o + x * p.X() + y * p.Y() + z * depth;
    return gp_Pnt(w);
}

ContourResult computeOuterContour(const TopoDS_Shape& shape, const ContourOptions& opts)
{
    ContourResult r;
    try {
        const gp_Ax2 viewCS(gp_Pnt(0.0, 0.0, 0.0), opts.direction);
        r.viewSystem = viewCS;

        // 1) HLR 分割边(2D@Z=0)。HLR 的 In3d=false 边只有 2D pcurve、无 3D 曲线,
        //    直接喂给 BOP 会崩;用 BRepLib::BuildCurves3d 从 pcurve 精确生成 3D 曲线。
        TopoDS_Compound edges = collectPartitionEdges(shape, viewCS);
        if (countEdges(edges) == 0) { r.message = "HLR 无投影边"; return r; }
        BRepLib::BuildCurves3d(edges);
        if (ocrlDebug()) std::printf("[STAGE] partition edges=%zu (3d curves built)\n", countEdges(edges));
        if (ocrlDebug()) {
            int idx = 0;
            for (TopExp_Explorer e(edges, TopAbs_EDGE); e.More(); e.Next(), ++idx) {
                const TopoDS_Edge ed = TopoDS::Edge(e.Current());
                Standard_Real f, l; Handle(Geom_Curve) c = BRep_Tool::Curve(ed, f, l);
                int nv = 0; for (TopExp_Explorer v(ed, TopAbs_VERTEX); v.More(); v.Next()) ++nv;
                Bnd_Box ebx; BRepBndLib::Add(ed, ebx, Standard_False);
                double a0=0,b0=0,c0=0,a1=0,b1=0,c1=0; if (!ebx.IsVoid()) ebx.Get(a0,b0,c0,a1,b1,c1);
                std::printf("[EDGE %d] curve3dNull=%d nVerts=%d Z=[%.4f,%.4f] deg=%d\n",
                            idx, c.IsNull()?1:0, nv, c0, c1, BRep_Tool::Degenerated(ed)?1:0);
            }
        }

        // 2) 边的 2D 包围盒 -> 视平面(全局 XY)上的 base 矩形面
        Bnd_Box eb; BRepBndLib::Add(edges, eb, Standard_False);
        if (eb.IsVoid()) { r.message = "投影边包围盒为空"; return r; }
        double x0, y0, z0, x1, y1, z1; eb.Get(x0, y0, z0, x1, y1, z1);
        const double margin = 0.05 * std::max(x1 - x0, y1 - y0) + 1.0;
        gp_Pln pln(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)));
        BRepBuilderAPI_MakeFace mkBase(pln, x0 - margin, x1 + margin, y0 - margin, y1 + margin);
        if (!mkBase.IsDone()) { r.message = "base 面构造失败"; return r; }
        const TopoDS_Face base = mkBase.Face();
        if (ocrlDebug()) std::printf("[STAGE] base face ok (bbox %.1f..%.1f, %.1f..%.1f)\n", x0, x1, y0, y1);

        // 3) 用边把 base 面切成子面
        NCollection_List<TopoDS_Shape> args, tools;
        args.Append(base);
        for (TopExp_Explorer e(edges, TopAbs_EDGE); e.More(); e.Next()) tools.Append(e.Current());
        BRepAlgoAPI_Splitter splitter;
        splitter.SetArguments(args);
        splitter.SetTools(tools);
        splitter.SetFuzzyValue(1.0e-4); // 吸收 HLR 边的微小缝隙
        if (ocrlDebug()) std::printf("[STAGE] calling splitter.Build() with %d tool edges...\n", tools.Extent());
        splitter.Build();
        if (ocrlDebug()) std::printf("[STAGE] splitter built, hasErrors=%d\n", splitter.HasErrors() ? 1 : 0);
        if (splitter.HasErrors()) { r.message = "平面分割失败"; return r; }

        // 4) 逐子面分类:取面内一点,映射回世界,沿视向投射线是否击中实体
        IntCurvesFace_ShapeIntersector inter;
        inter.Load(shape, Precision::Confusion());
        NCollection_List<TopoDS_Shape> shadow;
        std::size_t nSub = 0;
        for (TopExp_Explorer fe(splitter.Shape(), TopAbs_FACE); fe.More(); fe.Next()) {
            ++nSub;
            const TopoDS_Face f = TopoDS::Face(fe.Current());
            gp_Pnt pin; double param = 0.5;
            bool got = BRepClass3d_SolidExplorer::FindAPointInTheFace(f, pin, param);
            if (!got)
                for (double pp : {0.25, 0.75, 0.1, 0.9}) {
                    param = pp;
                    if (BRepClass3d_SolidExplorer::FindAPointInTheFace(f, pin, param)) { got = true; break; }
                }
            if (!got) continue;
            const gp_Pnt worldP = mapViewToWorld(viewCS, pin, 0.0);
            inter.Perform(gp_Lin(worldP, opts.direction), -Precision::Infinite(), Precision::Infinite());
            if (inter.IsDone() && inter.NbPnt() > 0) shadow.Append(f); // 命中=在阴影内
        }
        if (ocrlDebug())
            std::printf("[REGION] subFaces=%zu shadowFaces=%d\n", nSub, shadow.Extent());
        if (shadow.IsEmpty()) { r.message = "无阴影子面"; return r; }

        // 5) 并集所有阴影子面 + 合并同域
        TopoDS_Shape region;
        if (shadow.Extent() == 1) {
            region = shadow.First();
        } else {
            NCollection_List<TopoDS_Shape> grpA, grpB;
            bool toB = false;
            for (NCollection_List<TopoDS_Shape>::Iterator it(shadow); it.More(); it.Next()) {
                (toB ? grpB : grpA).Append(it.Value());
                toB = !toB;
            }
            BRepAlgoAPI_Fuse fuse;
            fuse.SetArguments(grpA);
            fuse.SetTools(grpB);
            fuse.Build();
            if (fuse.HasErrors()) { r.message = "阴影面并集失败"; return r; }
            region = fuse.Shape();
        }
        ShapeUpgrade_UnifySameDomain uni(region, Standard_True, Standard_True, Standard_False);
        uni.Build();
        region = uni.Shape();

        // 6) 取区域中最大面的外环 + 内孔
        TopoDS_Face best; double bestArea = -1.0;
        for (TopExp_Explorer fe(region, TopAbs_FACE); fe.More(); fe.Next()) {
            const TopoDS_Face f = TopoDS::Face(fe.Current());
            GProp_GProps gp; BRepGProp::SurfaceProperties(f, gp);
            const double a = std::fabs(gp.Mass());
            if (a > bestArea) { bestArea = a; best = f; }
        }
        if (best.IsNull()) { r.message = "并集无面"; return r; }

        r.area = bestArea;
        const TopoDS_Wire outer = BRepTools::OuterWire(best);
        r.outerWire = outer;
        std::size_t loops = 0;
        for (TopExp_Explorer we(best, TopAbs_WIRE); we.More(); we.Next()) {
            const TopoDS_Wire w = TopoDS::Wire(we.Current());
            ++loops;
            if (outer.IsNull() || w.IsSame(outer)) continue;
            r.holeWires.push_back(w);
        }
        r.loopCount = loops;
        r.straightEdgeCount = countStraightEdges(outer);

        // 7) 有序离散 + 度量
        r.outerPolyline = wireToPolylineOrdered(outer, opts.angularDeflection, opts.curvatureDeflection);
        std::vector<Polyline> all{ r.outerPolyline };
        for (const auto& hw : r.holeWires) {
            Polyline hp = wireToPolylineOrdered(hw, opts.angularDeflection, opts.curvatureDeflection);
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

} // namespace ocrl
