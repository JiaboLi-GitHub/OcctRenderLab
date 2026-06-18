// 无窗口取证工具:计算外轮廓并导出为 SVG,直接看轮廓形状是否对。
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

#include <occtcontour/ModelLoad.h>
#include <occtcontour/OuterContour.h>

static void writePoly(std::ofstream& svg, const ocrl::Polyline& p,
                      double minY, double maxY, const char* stroke,
                      const char* fill, double sw)
{
    if (p.size() < 2) return;
    svg << "  <polygon fill=\"" << fill << "\" stroke=\"" << stroke
        << "\" stroke-width=\"" << sw << "\" stroke-linejoin=\"round\" points=\"";
    for (const auto& pt : p) {
        const double y = (minY + maxY) - pt.Y(); // SVG y 朝下,垂直翻转使其正立
        svg << pt.X() << "," << y << " ";
    }
    svg << "\"/>\n";
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: contour_svg <model.step> [dx dy dz] [-o out.svg]\n"); return 1; }
    const std::filesystem::path path = argv[1];
    gp_Dir dir(0, 0, 1);
    std::string out = "contour.svg";
    double defl = 0.0; // 0 => 用默认离散精度
    std::vector<std::string> da;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) out = argv[++i];
        else if (a == "--defl" && i + 1 < argc) defl = std::stod(argv[++i]);
        else da.push_back(a);
    }
    if (da.size() >= 3) dir = gp_Dir(std::stod(da[0]), std::stod(da[1]), std::stod(da[2]));

    TopoDS_Compound c = ocrl::loadCompound(path);
    Bnd_Box mb; BRepBndLib::Add(c, mb, Standard_False);
    double x0, y0, z0, x1, y1, z1; mb.Get(x0, y0, z0, x1, y1, z1);

    ocrl::ContourOptions o; o.direction = dir;
    if (defl > 0.0) { o.angularDeflection = defl; o.curvatureDeflection = defl; }
    ocrl::ContourResult r = ocrl::computeOuterContour(c, o);

    std::printf("model 3D bbox: X %.2f Y %.2f Z %.2f\n", x1 - x0, y1 - y0, z1 - z0);
    std::printf("contour: ok=%d loops=%zu area=%.2f  contour-bbox %.2f x %.2f  outerPts=%zu holes=%zu\n",
                r.ok, r.loopCount, r.area, r.bboxWidth(), r.bboxHeight(),
                r.outerPolyline.size(), r.holePolylines.size());
    if (!r.ok) { std::printf("no contour: %s\n", r.message.c_str()); return 1; }

    const double minX = r.bboxMinX, minY = r.bboxMinY, maxX = r.bboxMaxX, maxY = r.bboxMaxY;
    const double w = maxX - minX, h = maxY - minY, big = std::max(w, h);
    const double m = 0.05 * big, sw = 0.004 * big;

    std::ofstream svg(out);
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""
        << (minX - m) << " " << (minY - m) << " " << (w + 2 * m) << " " << (h + 2 * m) << "\">\n";
    writePoly(svg, r.outerPolyline, minY, maxY, "#1E66FF", "rgba(30,102,255,0.08)", sw); // 外环蓝(对照 Fusion)
    for (const auto& hp : r.holePolylines)
        writePoly(svg, hp, minY, maxY, "#E0322B", "white", sw);                          // 内孔红
    svg << "</svg>\n";
    std::printf("wrote %s\n", out.c_str());
    return 0;
}
