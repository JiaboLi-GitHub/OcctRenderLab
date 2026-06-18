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

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: contour_viewer <model.step> [dx dy dz] [--frames N]\n"); return 1; }
    const std::filesystem::path path = argv[1];
    gp_Dir dir(0, 0, 1);
    int maxFrames = 0;  // 0 = 交互模式(直到关闭);>0 = 渲染 N 帧后退出(用于自动验证)
    std::vector<std::string> dirArgs;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) { maxFrames = std::stoi(argv[++i]); }
        else dirArgs.push_back(a);
    }
    if (dirArgs.size() >= 3)
        dir = gp_Dir(std::stod(dirArgs[0]), std::stod(dirArgs[1]), std::stod(dirArgs[2]));

    using clock = std::chrono::steady_clock;

    // 1) 读 STEP(一次),分别供 实体场景 与 外轮廓
    auto t0 = clock::now();
    vsgocct::cad::AssemblyData assembly = vsgocct::cad::readStep(path);
    auto sceneData = vsgocct::scene::buildAssemblyScene(assembly);
    auto t1 = clock::now();

    TopoDS_Compound compound = ocrl::loadCompound(path);  // 拍平(再读一次, demo 可接受)
    ocrl::ContourOptions opts; opts.direction = dir;
    ocrl::ContourResult contour = ocrl::computeOuterContour(compound, opts);
    auto t2 = clock::now();

    auto ms = [](auto a, auto b){ return std::chrono::duration_cast<std::chrono::milliseconds>(b-a).count(); };
    std::printf("read+scene %lldms; contour %lldms; ok=%d loops=%zu area=%.3f msg=%s\n",
                (long long)ms(t0,t1), (long long)ms(t1,t2), contour.ok,
                contour.loopCount, contour.area, contour.message.c_str());

    // 2) 组场景:实体 + 轮廓(轮廓摆到模型外缘的投影平面)
    auto root = vsg::Group::create();
    if (sceneData.scene) root->addChild(sceneData.scene);

    const double depth = -sceneData.radius * 1.2; // 沿视向往后摆,像投影面
    if (contour.ok) {
        std::vector<ocrl::Polyline> outer{ contour.outerPolyline };
        root->addChild(ocrl::buildOutlineNode(contour.viewSystem, outer, vsg::vec3(1.0f,0.9f,0.1f), depth));
        if (!contour.holePolylines.empty())
            root->addChild(ocrl::buildOutlineNode(contour.viewSystem, contour.holePolylines, vsg::vec3(0.2f,0.7f,1.0f), depth));
    }

    // 3) viewer + 相机(由 center/radius 定位)
    auto traits = vsg::WindowTraits::create(1280, 960, "OcctRenderLab - Outer Contour");
    auto window = vsg::Window::create(traits);
    if (!window) { std::printf("failed to create window (check Vulkan)\n"); return 1; }

    const vsg::dvec3 centre = sceneData.center;
    double radius = sceneData.radius > 0 ? sceneData.radius : 1.0;
    double aspect = double(window->extent2D().width) / double(window->extent2D().height);
    auto lookAt = vsg::LookAt::create(centre + vsg::dvec3(0, -radius*3.0, radius*1.5), centre, vsg::dvec3(0,0,1));
    auto persp  = vsg::Perspective::create(45.0, aspect, radius*0.001, radius*12.0);
    auto camera = vsg::Camera::create(persp, lookAt, vsg::ViewportState::create(window->extent2D()));

    auto viewer = vsg::Viewer::create();
    viewer->addWindow(window);
    auto trackball = vsg::Trackball::create(camera);
    trackball->addWindow(window);
    viewer->addEventHandler(trackball);
    viewer->addEventHandler(vsg::CloseHandler::create(viewer));

    auto cg = vsg::createCommandGraphForView(window, camera, root);
    viewer->assignRecordAndSubmitTaskAndPresentation({cg});
    viewer->compile();

    int frame = 0;
    while (viewer->advanceToNextFrame()) {
        viewer->handleEvents();
        viewer->update();
        viewer->recordAndSubmit();
        viewer->present();
        if (maxFrames > 0 && ++frame >= maxFrames) {
            std::printf("rendered %d frames, exiting (--frames mode)\n", frame);
            break;
        }
    }
    return 0;
}
