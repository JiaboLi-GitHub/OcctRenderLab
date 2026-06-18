#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdio>
#include <string>

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

#include <occtcontour/ModelLoad.h>
#include <occtcontour/OuterContour.h>

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

static int g_fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("[FAIL] %s\n", msg); ++g_fails; } \
                              else { std::printf("[ok]   %s\n", msg); } } while(0)

static std::string data(const char* name) { return std::string(TEST_DATA_DIR) + "/" + name; }

static std::size_t countEdges(const TopoDS_Shape& s) {
    std::size_t n = 0;
    for (TopExp_Explorer e(s, TopAbs_EDGE); e.More(); e.Next()) ++n;
    return n;
}

// 单条折线自身的 2D 包围盒(用于断言外环本身是否横跨整件)
static void polyBBox(const ocrl::Polyline& p, double& w, double& h) {
    if (p.empty()) { w = h = 0; return; }
    double x0 = p[0].X(), x1 = x0, y0 = p[0].Y(), y1 = y0;
    for (const auto& pt : p) {
        x0 = std::min(x0, pt.X()); x1 = std::max(x1, pt.X());
        y0 = std::min(y0, pt.Y()); y1 = std::max(y1, pt.Y());
    }
    w = x1 - x0; h = y1 - y0;
}

static void test_model_load() {
    TopoDS_Compound c = ocrl::loadCompound(data("cylinder.step"));
    CHECK(!c.IsNull(), "loadCompound(cylinder) returns non-null compound");
    CHECK(countEdges(c) > 0, "cylinder compound has edges");
}

static void test_hlr_extract() {
    TopoDS_Compound c = ocrl::loadCompound(data("cylinder.step"));
    gp_Ax2 vs;
    TopoDS_Compound edges = ocrl::detail::extractVisibleOutlineEdges(c, gp_Dir(0, 0, 1), vs);
    CHECK(!edges.IsNull(), "HLR top-view extract returns non-null");
    CHECK(countEdges(edges) > 0, "HLR top-view has visible edges");
}

static void test_discretize() {
    TopoDS_Compound c = ocrl::loadCompound(data("sphere.step"));
    gp_Ax2 vs;
    TopoDS_Compound edges = ocrl::detail::extractVisibleOutlineEdges(c, gp_Dir(0, 0, 1), vs);
    std::size_t pts = 0;
    for (TopExp_Explorer e(edges, TopAbs_EDGE); e.More(); e.Next()) {
        auto poly = ocrl::detail::discretizeEdge(TopoDS::Edge(e.Current()), 0.05, 0.05);
        CHECK(poly.size() >= 2, "each edge discretized to >= 2 points");
        pts += poly.size();
    }
    CHECK(pts > 8, "sphere silhouette circle gets enough sampled points");
}

static void test_assemble_cylinder_top() {
    TopoDS_Compound c = ocrl::loadCompound(data("cylinder.step"));
    gp_Ax2 vs;
    TopoDS_Compound edges = ocrl::detail::extractVisibleOutlineEdges(c, gp_Dir(0, 0, 1), vs);
    auto loops = ocrl::detail::assembleClosedLoops(edges, 1e-4);
    CHECK(loops.outerIndex >= 0, "cylinder top: outer closed loop found");
    if (loops.outerIndex >= 0) {
        const double area = loops.areas[loops.outerIndex];
        std::printf("       (cylinder top outer area = %.4f, expect 4pi = %.4f)\n", area, M_PI * 4.0);
        CHECK(std::fabs(area - M_PI * 4.0) < 0.2, "cylinder top outer area ~= 4pi");
    }
}

static void test_full_pipeline() {
    using namespace ocrl;

    // 圆柱顶视 -> 圆:loops=1, area/bbox≈π/4, aspect≈1, 直边=0
    {
        ContourOptions o; o.direction = gp_Dir(0, 0, 1);
        ContourResult r = computeOuterContour(loadCompound(data("cylinder.step")), o);
        CHECK(r.ok, "cylinder top ok");
        CHECK(r.loopCount == 1, "cylinder top single loop");
        CHECK(std::fabs(r.area / r.bboxArea() - M_PI / 4.0) < 0.02, "cylinder top area/bbox ~= pi/4");
        CHECK(std::fabs(r.aspect() - 1.0) < 0.02, "cylinder top aspect ~= 1");
        CHECK(r.straightEdgeCount == 0, "cylinder top no straight edges (pure circle)");
    }
    // 圆柱侧视 -> 矩形 4x10:area/bbox≈1, aspect≈2.5
    {
        ContourOptions o; o.direction = gp_Dir(1, 0, 0);
        ContourResult r = computeOuterContour(loadCompound(data("cylinder.step")), o);
        CHECK(r.ok, "cylinder side ok");
        std::printf("       (cylinder side area/bbox=%.4f aspect=%.4f)\n",
                    r.bboxArea() > 0 ? r.area / r.bboxArea() : 0.0, r.aspect());
        CHECK(std::fabs(r.area / r.bboxArea() - 1.0) < 0.05, "cylinder side area/bbox ~= 1 (rect)");
        CHECK(std::fabs(r.aspect() - 2.5) < 0.1, "cylinder side aspect ~= 2.5");
    }
    // 球体任意视 -> 圆 R=5:area/bbox≈π/4, aspect≈1, 直边=0
    {
        ContourOptions o; o.direction = gp_Dir(0.3, 0.4, 0.866);
        ContourResult r = computeOuterContour(loadCompound(data("sphere.step")), o);
        CHECK(r.ok, "sphere ok");
        CHECK(r.loopCount == 1, "sphere single loop");
        CHECK(std::fabs(r.area / r.bboxArea() - M_PI / 4.0) < 0.02, "sphere area/bbox ~= pi/4");
        CHECK(r.straightEdgeCount == 0, "sphere no straight edges");
    }
    // 严谨不变量:外轮廓的 2D 外接框 必须 = 实体在投影平面的 3D 外接框范围。
    // 薄板顶视(沿 Z):真实 3D bbox = 100 × 99.51 × 2.5 -> 投影 ≈ 100×99.5(aspect≈1)
    {
        ContourOptions o; o.direction = gp_Dir(0, 0, 1);
        ContourResult r = computeOuterContour(loadCompound(data("plate.step")), o);
        CHECK(r.ok, "plate ok");
        const double mx = std::max(r.bboxWidth(), r.bboxHeight());
        std::printf("       (plate loops=%zu maxdim=%.2f aspect=%.4f area=%.1f)\n",
                    r.loopCount, mx, r.aspect(), r.area);
        CHECK(std::fabs(mx - 100.0) < 3.0, "plate contour max dim ~= 100 (matches 3D bbox)");
        CHECK(std::fabs(r.aspect() - 1.005) < 0.1, "plate contour aspect ~= 1 (square plate)");
        CHECK(r.area > 5000.0, "plate outer area substantial (>5000), not a tiny feature");
    }
    // 长条顶视(沿 Z):真实 3D bbox = 26 × 113 × 3 -> 投影 ≈ 26×113(aspect≈4.35)
    {
        ContourOptions o; o.direction = gp_Dir(0, 0, 1);
        ContourResult r = computeOuterContour(loadCompound(data("bar.step")), o);
        CHECK(r.ok, "bar ok");
        const double mx = std::max(r.bboxWidth(), r.bboxHeight());
        std::printf("       (bar loops=%zu maxdim=%.2f aspect=%.4f area=%.1f)\n",
                    r.loopCount, mx, r.aspect(), r.area);
        CHECK(std::fabs(mx - 113.0) < 4.0, "bar contour max dim ~= 113 (matches 3D bbox)");
        CHECK(std::fabs(r.aspect() - 4.35) < 0.6, "bar contour aspect ~= 4.35");
        CHECK(r.loopCount >= 1, "bar at least one loop");
    }
}

static void diag_bbox(const char* name) {
    TopoDS_Compound c = ocrl::loadCompound(data(name));
    Bnd_Box b; BRepBndLib::Add(c, b, Standard_False);
    double x0,y0,z0,x1,y1,z1; b.Get(x0,y0,z0,x1,y1,z1);
    std::printf("       [bbox] %-14s X %.2f Y %.2f Z %.2f\n", name, x1-x0, y1-y0, z1-z0);
}

// 回归测试:复杂件(骷髅匕首)的外环本身必须横跨整件(198mm),而不是片段。
// 旧的"HLR边拼环+取最大面积"会把刀身误判成内孔,外环只剩手柄段(~164mm)。
static void test_knife_outer_complete() {
    using namespace ocrl;
    ContourOptions o; o.direction = gp_Dir(0, 0, 1);  // 顶视
    ContourResult r = computeOuterContour(loadCompound(data("knife.step")), o);
    CHECK(r.ok, "knife ok");
    double ow, oh; polyBBox(r.outerPolyline, ow, oh);
    std::printf("       (knife OUTER-loop bbox = %.2f x %.2f, full part = 198.05 x 37.24)\n", ow, oh);
    CHECK(ow > 190.0, "knife outer loop spans full length ~198 (not a 164mm fragment)");
    CHECK(oh > 33.0,  "knife outer loop spans full width ~37");
    // 外环应是一条完整闭环:首尾点几乎重合
    if (r.outerPolyline.size() >= 2) {
        const double gap = r.outerPolyline.front().Distance(r.outerPolyline.back());
        CHECK(gap < 1.0, "knife outer loop is closed (first~=last point)");
    }
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);  // 不缓冲,崩溃时也能看到最后一行
    std::printf("=== 3D bounding boxes ===\n");
    diag_bbox("cylinder.step");
    diag_bbox("sphere.step");
    diag_bbox("plate.step");
    diag_bbox("bar.step");
    std::printf("=========================\n");
    test_model_load();
    test_hlr_extract();
    test_discretize();
    test_assemble_cylinder_top();
    test_full_pipeline();
    test_knife_outer_complete();
    std::printf(g_fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", g_fails);
    return g_fails ? 1 : 0;
}
