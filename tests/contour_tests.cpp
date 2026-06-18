#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdio>
#include <string>

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include "ModelLoad.h"

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

static void test_model_load() {
    TopoDS_Compound c = ocrl::loadCompound(data("cylinder.step"));
    CHECK(!c.IsNull(), "loadCompound(cylinder) returns non-null compound");
    CHECK(countEdges(c) > 0, "cylinder compound has edges");
}

int main() {
    test_model_load();
    std::printf(g_fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", g_fails);
    return g_fails ? 1 : 0;
}
