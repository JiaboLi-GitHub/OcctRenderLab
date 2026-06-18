#include "OutlineRender.h"

namespace ocrl {

static const char* OUTLINE_VERT = R"(#version 450
layout(push_constant) uniform PC { mat4 projection; mat4 modelView; };
layout(location=0) in vec3 vertex;
layout(location=1) in vec3 inColor;
layout(location=0) out vec3 col;
out gl_PerVertex { vec4 gl_Position; };
void main(){ col = inColor; gl_Position = projection * (modelView * vec4(vertex,1.0)); }
)";

static const char* OUTLINE_FRAG = R"(#version 450
layout(location=0) in vec3 col;
layout(location=0) out vec4 o;
void main(){ o = vec4(col, 1.0); }
)";

vsg::ref_ptr<vsg::BindGraphicsPipeline> createLinePipeline()
{
    auto layout = vsg::PipelineLayout::create(
        vsg::DescriptorSetLayouts{},
        vsg::PushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 128}});
    auto vs = vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT,   "main", OUTLINE_VERT);
    auto fs = vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", OUTLINE_FRAG);

    auto vis = vsg::VertexInputState::create();
    vis->vertexBindingDescriptions.push_back({0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
    vis->vertexBindingDescriptions.push_back({1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
    vis->vertexAttributeDescriptions.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
    vis->vertexAttributeDescriptions.push_back({1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0});

    auto ias = vsg::InputAssemblyState::create();
    ias->topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    auto rs = vsg::RasterizationState::create();
    rs->cullMode = VK_CULL_MODE_NONE; rs->lineWidth = 1.0f;
    auto ds = vsg::DepthStencilState::create();
    ds->depthTestEnable = VK_TRUE; ds->depthWriteEnable = VK_TRUE;
    ds->depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;   // VSG 反向 Z

    vsg::GraphicsPipelineStates states{
        vis, ias, rs, vsg::ColorBlendState::create(),
        vsg::MultisampleState::create(), ds};
    auto gp = vsg::GraphicsPipeline::create(layout, vsg::ShaderStages{vs, fs}, states);
    return vsg::BindGraphicsPipeline::create(gp);
}

vsg::ref_ptr<vsg::Node> buildOutlineNode(const gp_Ax2& vs,
                                         const std::vector<Polyline>& polylines,
                                         const vsg::vec3& color,
                                         double depth)
{
    // 把每条折线的顺序点转为 LINE_LIST 段对:相邻点 (p[i],p[i+1]) 各成一段。
    std::vector<vsg::vec3> verts;
    for (const auto& poly : polylines) {
        if (poly.size() < 2) continue;
        for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
            const gp_Pnt a = mapViewToWorld(vs, poly[i],   depth);
            const gp_Pnt b = mapViewToWorld(vs, poly[i+1], depth);
            verts.emplace_back(float(a.X()), float(a.Y()), float(a.Z()));
            verts.emplace_back(float(b.X()), float(b.Y()), float(b.Z()));
        }
    }
    auto sg = vsg::StateGroup::create();
    sg->add(createLinePipeline());
    if (verts.empty()) return sg;

    const uint32_t n = static_cast<uint32_t>(verts.size());
    auto positions = vsg::vec3Array::create(n);
    auto colors    = vsg::vec3Array::create(n);
    auto indices   = vsg::uintArray::create(n);
    for (uint32_t i = 0; i < n; ++i) {
        (*positions)[i] = verts[i]; (*colors)[i] = color; (*indices)[i] = i;
    }
    auto draw = vsg::VertexIndexDraw::create();
    draw->assignArrays(vsg::DataList{positions, colors});
    draw->assignIndices(indices);
    draw->indexCount = indices->width();
    draw->instanceCount = 1;
    sg->addChild(draw);
    return sg;
}

} // namespace ocrl
