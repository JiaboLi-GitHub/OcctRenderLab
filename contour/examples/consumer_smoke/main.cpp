// 最小外部消费示例:用 find_package(occtcontour) 链接算法库,算一次外轮廓。
#include <occtcontour/ModelLoad.h>
#include <occtcontour/OuterContour.h>
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: consumer_smoke <model.step>\n"); return 1; }
    TopoDS_Compound c = ocrl::loadCompound(argv[1]);
    ocrl::ContourResult r = ocrl::computeOuterContour(c, {});
    std::printf("consumer ok=%d loops=%zu area=%.3f\n", r.ok, r.loopCount, r.area);
    return r.ok ? 0 : 1;
}
