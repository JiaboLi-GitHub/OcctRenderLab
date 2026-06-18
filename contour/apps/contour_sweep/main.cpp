#include <chrono>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>

#include <Standard_Failure.hxx>

#include <occtcontour/ModelLoad.h>
#include <occtcontour/OuterContour.h>

namespace fs = std::filesystem;

static bool isStep(const fs::path& p) {
    std::string e = p.extension().string();
    for (auto& c : e) c = (char)std::tolower((unsigned char)c);
    return e == ".step" || e == ".stp";
}

int main(int argc, char** argv)
{
    const std::string dir = (argc >= 2) ? argv[1] : "D:/model/12";
    int total = 0, ok = 0, fail = 0;

    std::error_code ec;
    fs::recursive_directory_iterator it(dir, ec), end;
    if (ec) { std::printf("cannot open dir: %s (%s)\n", dir.c_str(), ec.message().c_str()); return 0; }

    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const fs::path p = it->path();
        if (!fs::is_regular_file(p, ec) || !isStep(p)) continue;
        ++total;
        const std::string name = p.filename().u8string();
        try {
            auto t0 = std::chrono::steady_clock::now();
            TopoDS_Compound c = ocrl::loadCompound(p);
            ocrl::ContourResult r = ocrl::computeOuterContour(c, {});
            auto t1 = std::chrono::steady_clock::now();
            long long msv = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            if (r.ok) { ++ok;  std::printf("[OK  %5lldms] loops=%2zu area=%11.2f  %s\n", msv, r.loopCount, r.area, name.c_str()); }
            else      { ++fail; std::printf("[FAIL        ] %s  (%s)\n", name.c_str(), r.message.c_str()); }
        } catch (const Standard_Failure& e) {
            ++fail; std::printf("[CRASH-OCCT  ] %s  (%s)\n", name.c_str(), e.GetMessageString());
        } catch (const std::exception& e) {
            ++fail; std::printf("[CRASH       ] %s  (%s)\n", name.c_str(), e.what());
        }
    }
    std::printf("\ntotal %d, ok %d, fail %d\n", total, ok, fail);
    return 0; // 扫描工具永远 0 退出(失败计入统计,不算崩溃)
}
