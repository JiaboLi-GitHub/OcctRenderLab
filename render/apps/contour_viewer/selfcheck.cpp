#include <cstdio>
#include <ocrlrender/OutlineRender.h>

int main() {
    // 一条折线 3 个点 => 2 段 => 4 个顶点
    ocrl::Polyline poly = { gp_Pnt(0,0,0), gp_Pnt(1,0,0), gp_Pnt(1,1,0) };
    gp_Ax2 vs(gp_Pnt(0,0,0), gp_Dir(0,0,1));
    auto node = ocrl::buildOutlineNode(vs, { poly }, vsg::vec3(1,1,0), 0.0);
    int fails = 0;
    if (!node) { std::printf("[FAIL] node is null\n"); ++fails; }
    else std::printf("[ok] buildOutlineNode non-null (3-pt polyline = 2 segments = 4 verts)\n");
    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
