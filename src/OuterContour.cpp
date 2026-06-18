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
        if (area > maxArea)
        {
            maxArea = area;
            result.outerIndex = static_cast<int>(result.closedWires.size()) - 1;
        }
    }
    return result;
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
