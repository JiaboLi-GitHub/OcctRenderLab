#pragma once
#include <vector>
#include <vsg/all.h>
#include <gp_Pnt.hxx>
#include "OuterContour.h"

namespace ocrl {
// 用一组折线(每条按顺序点)构建 LINE_LIST 轮廓节点。color 为线色。
// depth:沿视向把 2D 视平面点摆到世界平面的深度(见 mapViewToWorld)。
vsg::ref_ptr<vsg::Node> buildOutlineNode(const gp_Ax2& viewSystem,
                                         const std::vector<Polyline>& polylines,
                                         const vsg::vec3& color,
                                         double depth);
// 暴露线管线创建(viewer 复用)。
vsg::ref_ptr<vsg::BindGraphicsPipeline> createLinePipeline();
}
