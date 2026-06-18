#include "OuterContour.h"

#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>

#include <BRep_Builder.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_TangentialDeflection.hxx>
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
