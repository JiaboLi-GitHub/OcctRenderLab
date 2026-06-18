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

// 由外轮廓结果构建轮廓子树(外环亮黄 + 孔青色),摆到沿视向 depth 处。
vsg::ref_ptr<vsg::Node> buildContourNodes(const ocrl::ContourResult& r, double depth)
{
    auto g = vsg::Group::create();
    if (!r.ok) return g;
    std::vector<ocrl::Polyline> outer{ r.outerPolyline };
    g->addChild(ocrl::buildOutlineNode(r.viewSystem, outer, vsg::vec3(1.0f, 0.9f, 0.1f), depth));
    if (!r.holePolylines.empty())
        g->addChild(ocrl::buildOutlineNode(r.viewSystem, r.holePolylines, vsg::vec3(0.2f, 0.7f, 1.0f), depth));
    return g;
}

// 键盘:1-6/0 切投影方向并运行时重算;F 切实体、C 切轮廓、M 切原位/投影平面。
class ContourKeyHandler : public vsg::Inherit<vsg::Visitor, ContourKeyHandler>
{
public:
    vsg::observer_ptr<vsg::Viewer> viewer;
    vsg::ref_ptr<vsg::Group>  contourGroup;
    vsg::ref_ptr<vsg::Switch> solidSwitch;
    vsg::ref_ptr<vsg::Switch> contourSwitch;
    TopoDS_Compound compound;
    double radius = 1.0;

    void setDirection(const gp_Dir& d) { m_dir = d; recompute(); }

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
            case vsg::KEY_f: if (solidSwitch)   { m_solid = !m_solid;     solidSwitch->setAllChildren(m_solid); } break;
            case vsg::KEY_c: if (contourSwitch) { m_contour = !m_contour; contourSwitch->setAllChildren(m_contour); } break;
            case vsg::KEY_m: m_flatten = !m_flatten; recompute(); break;
            default: break;
        }
    }

private:
    gp_Dir m_dir{0, 0, 1};
    bool m_solid = true, m_contour = true, m_flatten = true;

    void recompute()
    {
        ocrl::ContourOptions o; o.direction = m_dir;
        ocrl::ContourResult r = ocrl::computeOuterContour(compound, o);
        std::printf("recompute: ok=%d loops=%zu area=%.3f msg=%s\n",
                    r.ok, r.loopCount, r.area, r.message.c_str());

        const double depth = m_flatten ? -radius * 1.2 : 0.0;
        contourGroup->children.clear();
        contourGroup->addChild(buildContourNodes(r, depth));

        // 运行时编译新增几何并并入 viewer(vsgExamples dynamicload 范式)
        if (auto v = viewer.ref_ptr()) {
            auto cr = v->compileManager->compile(contourGroup);
            vsg::updateViewer(*v, cr);
        }
    }
};

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: contour_viewer <model.step> [dx dy dz] [--frames N] [--cycle]\n"); return 1; }
    const std::filesystem::path path = argv[1];
    gp_Dir dir(0, 0, 1);
    int maxFrames = 0;     // 0 = 交互;>0 = 渲染 N 帧退出(自动验证)
    bool cycle = false;    // 自动验证:运行中切几次方向,触发运行时重算路径
    std::vector<std::string> dirArgs;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) maxFrames = std::stoi(argv[++i]);
        else if (a == "--cycle") cycle = true;
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
    std::printf("read+scene %lldms; contour %lldms; ok=%d loops=%zu area=%.3f msg=%s\n",
                (long long)ms(t0,t1), (long long)ms(t1,t2), contour.ok,
                contour.loopCount, contour.area, contour.message.c_str());
    std::printf("keys: 1-6/0=projection dir, F=solid, C=contour, M=flatten/in-place, Esc=quit\n");

    double radius = sceneData.radius > 0 ? sceneData.radius : 1.0;
    const double depth0 = -radius * 1.2;

    // 场景图:solidSwitch(实体) + contourSwitch(contourGroup -> 轮廓)
    auto root = vsg::Group::create();
    auto solidSwitch = vsg::Switch::create();
    if (sceneData.scene) solidSwitch->addChild(true, sceneData.scene);
    root->addChild(solidSwitch);

    auto contourGroup = vsg::Group::create();
    contourGroup->addChild(buildContourNodes(contour, depth0));
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

    auto trackball = vsg::Trackball::create(camera);
    trackball->addWindow(window);
    viewer->addEventHandler(trackball);
    viewer->addEventHandler(keyHandler);
    viewer->addEventHandler(vsg::CloseHandler::create(viewer));

    auto cg = vsg::createCommandGraphForView(window, camera, root);
    viewer->assignRecordAndSubmitTaskAndPresentation({cg});
    viewer->compile();
    keyHandler->viewer = viewer;  // compileManager 此时已就绪

    int frame = 0;
    while (viewer->advanceToNextFrame()) {
        viewer->handleEvents();
        viewer->update();
        viewer->recordAndSubmit();
        viewer->present();
        ++frame;
        // 自动验证:在运行中切换投影方向,触发 compileManager->compile + updateViewer
        if (cycle && frame == 3) keyHandler->setDirection(gp_Dir(1, 0, 0));
        if (cycle && frame == 6) keyHandler->setDirection(gp_Dir(0, 0, 1));
        if (maxFrames > 0 && frame >= maxFrames) {
            std::printf("rendered %d frames, exiting (--frames mode)\n", frame);
            break;
        }
    }
    return 0;
}
