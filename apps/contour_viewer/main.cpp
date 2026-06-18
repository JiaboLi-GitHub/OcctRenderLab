#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <vsg/all.h>

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/scene/SceneBuilder.h>

#include "ModelLoad.h"
#include "OuterContour.h"
#include "OutlineRender.h"

namespace {

// 回路显示模式:全部 / 仅外回路 / 仅内回路(孔)。
enum class LoopMode { All, Outer, Inner };
const char* loopName(LoopMode m) { return m == LoopMode::All ? "All" : (m == LoopMode::Outer ? "Outer" : "Inner"); }
LoopMode parseLoopMode(const std::string& s) {
    if (s == "outer") return LoopMode::Outer;
    if (s == "inner") return LoopMode::Inner;
    return LoopMode::All;
}

// 按模式构建轮廓子树(外环亮黄、内孔青色),摆到沿视向 depth 处。纯显示筛选,不重算几何。
vsg::ref_ptr<vsg::Node> buildContourNodes(const ocrl::ContourResult& r, double depth, LoopMode mode)
{
    auto g = vsg::Group::create();
    if (!r.ok) return g;
    if (mode != LoopMode::Inner) {  // All 或 Outer -> 画外环
        std::vector<ocrl::Polyline> outer{ r.outerPolyline };
        g->addChild(ocrl::buildOutlineNode(r.viewSystem, outer, vsg::vec3(1.0f, 0.9f, 0.1f), depth));
    }
    if (mode != LoopMode::Outer && !r.holePolylines.empty()) {  // All 或 Inner -> 画内孔
        g->addChild(ocrl::buildOutlineNode(r.viewSystem, r.holePolylines, vsg::vec3(0.2f, 0.7f, 1.0f), depth));
    }
    return g;
}

// 键盘:1-6/0 切投影方向(重算 HLR);7/8/9 切 全部/外/内 回路(仅重建显示);
//      F 切实体、C 切轮廓、M 切原位/投影平面。
class ContourKeyHandler : public vsg::Inherit<vsg::Visitor, ContourKeyHandler>
{
public:
    vsg::observer_ptr<vsg::Viewer> viewer;
    vsg::ref_ptr<vsg::Group>  contourGroup;
    vsg::ref_ptr<vsg::Switch> solidSwitch;
    vsg::ref_ptr<vsg::Switch> contourSwitch;
    TopoDS_Compound compound;
    double radius = 1.0;

    // 用 main 已算好的初始结果与模式播种,避免启动时重复计算。
    void seed(const ocrl::ContourResult& r, LoopMode loop) { m_result = r; m_loop = loop; }

    void setDirection(const gp_Dir& d)  // 几何变了 -> 重算 HLR
    {
        m_dir = d;
        ocrl::ContourOptions o; o.direction = m_dir;
        m_result = ocrl::computeOuterContour(compound, o);
        std::printf("recompute: ok=%d loops=%zu area=%.3f msg=%s\n",
                    m_result.ok, m_result.loopCount, m_result.area, m_result.message.c_str());
        rebuild();
    }
    void setLoopMode(LoopMode m) { m_loop = m; std::printf("loops: %s\n", loopName(m)); rebuild(); } // 仅显示筛选
    void toggleFlatten() { m_flatten = !m_flatten; rebuild(); }  // 摆放参数,无需重算

    void apply(vsg::KeyPressEvent& key) override
    {
        switch (key.keyBase) {
            case vsg::KEY_1: setDirection(gp_Dir(0, 0, 1));  break; // 顶
            case vsg::KEY_2: setDirection(gp_Dir(0, 0, -1)); break; // 底
            case vsg::KEY_3: setDirection(gp_Dir(0, -1, 0)); break; // 前
            case vsg::KEY_4: setDirection(gp_Dir(0, 1, 0));  break; // 后
            case vsg::KEY_5: setDirection(gp_Dir(-1, 0, 0)); break; // 左
            case vsg::KEY_6: setDirection(gp_Dir(1, 0, 0));  break; // 右
            case vsg::KEY_0: setDirection(gp_Dir(1, 1, 1));  break; // 等轴测
            case vsg::KEY_7: setLoopMode(LoopMode::All);   break;   // 全部回路
            case vsg::KEY_8: setLoopMode(LoopMode::Outer); break;   // 仅外回路
            case vsg::KEY_9: setLoopMode(LoopMode::Inner); break;   // 仅内回路
            case vsg::KEY_f: if (solidSwitch)   { m_solid = !m_solid;     solidSwitch->setAllChildren(m_solid); } break;
            case vsg::KEY_c: if (contourSwitch) { m_contour = !m_contour; contourSwitch->setAllChildren(m_contour); } break;
            case vsg::KEY_m: toggleFlatten(); break;
            default: break;
        }
    }

private:
    gp_Dir m_dir{0, 0, 1};
    LoopMode m_loop = LoopMode::All;
    ocrl::ContourResult m_result;
    bool m_solid = true, m_contour = true, m_flatten = true;

    void rebuild()
    {
        const double depth = m_flatten ? -radius * 1.2 : 0.0;
        contourGroup->children.clear();
        contourGroup->addChild(buildContourNodes(m_result, depth, m_loop));
        if (auto v = viewer.ref_ptr()) {
            auto cr = v->compileManager->compile(contourGroup);
            vsg::updateViewer(*v, cr);
        }
    }
};

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: contour_viewer <model.step> [dx dy dz] [--frames N] [--cycle] [--loops all|outer|inner]\n");
        return 1;
    }
    const std::filesystem::path path = argv[1];
    gp_Dir dir(0, 0, 1);
    int maxFrames = 0;
    bool cycle = false;
    LoopMode loopMode = LoopMode::All;
    std::vector<std::string> dirArgs;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) maxFrames = std::stoi(argv[++i]);
        else if (a == "--cycle") cycle = true;
        else if (a == "--loops" && i + 1 < argc) loopMode = parseLoopMode(argv[++i]);
        else dirArgs.push_back(a);
    }
    if (dirArgs.size() >= 3)
        dir = gp_Dir(std::stod(dirArgs[0]), std::stod(dirArgs[1]), std::stod(dirArgs[2]));

    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    vsgocct::cad::AssemblyData assembly = vsgocct::cad::readStep(path);
    auto sceneData = vsgocct::scene::buildAssemblyScene(assembly);
    auto t1 = clock::now();

    TopoDS_Compound compound = ocrl::loadCompound(path);
    ocrl::ContourOptions opts; opts.direction = dir;
    ocrl::ContourResult contour = ocrl::computeOuterContour(compound, opts);
    auto t2 = clock::now();

    auto ms = [](auto a, auto b){ return std::chrono::duration_cast<std::chrono::milliseconds>(b-a).count(); };
    std::printf("read+scene %lldms; contour %lldms; ok=%d loops=%zu area=%.3f loopmode=%s msg=%s\n",
                (long long)ms(t0,t1), (long long)ms(t1,t2), contour.ok,
                contour.loopCount, contour.area, loopName(loopMode), contour.message.c_str());
    std::printf("keys: 1-6/0=projection dir, 7/8/9=loops all/outer/inner, F=solid, C=contour, M=flatten, Esc=quit\n");

    double radius = sceneData.radius > 0 ? sceneData.radius : 1.0;
    const double depth0 = -radius * 1.2;

    auto root = vsg::Group::create();
    auto solidSwitch = vsg::Switch::create();
    if (sceneData.scene) solidSwitch->addChild(true, sceneData.scene);
    root->addChild(solidSwitch);

    auto contourGroup = vsg::Group::create();
    contourGroup->addChild(buildContourNodes(contour, depth0, loopMode));
    auto contourSwitch = vsg::Switch::create();
    contourSwitch->addChild(true, contourGroup);
    root->addChild(contourSwitch);

    auto traits = vsg::WindowTraits::create(1280, 960, "OcctRenderLab - Outer Contour");
    auto window = vsg::Window::create(traits);
    if (!window) { std::printf("failed to create window (check Vulkan)\n"); return 1; }

    const vsg::dvec3 centre = sceneData.center;
    double aspect = double(window->extent2D().width) / double(window->extent2D().height);
    auto lookAt = vsg::LookAt::create(centre + vsg::dvec3(0, -radius*3.0, radius*1.5), centre, vsg::dvec3(0,0,1));
    auto persp  = vsg::Perspective::create(45.0, aspect, radius*0.001, radius*12.0);
    auto camera = vsg::Camera::create(persp, lookAt, vsg::ViewportState::create(window->extent2D()));

    auto viewer = vsg::Viewer::create();
    viewer->addWindow(window);

    auto keyHandler = ContourKeyHandler::create();
    keyHandler->contourGroup = contourGroup;
    keyHandler->solidSwitch = solidSwitch;
    keyHandler->contourSwitch = contourSwitch;
    keyHandler->compound = compound;
    keyHandler->radius = radius;
    keyHandler->seed(contour, loopMode);

    auto trackball = vsg::Trackball::create(camera);
    trackball->addWindow(window);
    viewer->addEventHandler(trackball);
    viewer->addEventHandler(keyHandler);
    viewer->addEventHandler(vsg::CloseHandler::create(viewer));

    auto cg = vsg::createCommandGraphForView(window, camera, root);
    viewer->assignRecordAndSubmitTaskAndPresentation({cg});
    viewer->compile();
    keyHandler->viewer = viewer;

    int frame = 0;
    while (viewer->advanceToNextFrame()) {
        viewer->handleEvents();
        viewer->update();
        viewer->recordAndSubmit();
        viewer->present();
        ++frame;
        if (cycle && frame == 3) keyHandler->setDirection(gp_Dir(1, 0, 0));
        if (cycle && frame == 6) keyHandler->setLoopMode(LoopMode::Outer);
        if (cycle && frame == 9) keyHandler->setLoopMode(LoopMode::Inner);
        if (cycle && frame == 12) keyHandler->setLoopMode(LoopMode::All);
        if (maxFrames > 0 && frame >= maxFrames) {
            std::printf("rendered %d frames, exiting (--frames mode)\n", frame);
            break;
        }
    }
    return 0;
}
