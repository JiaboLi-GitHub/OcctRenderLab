#include "ModelLoad.h"

#include <vsgocct/cad/StepReader.h>

#include <BRep_Builder.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>

namespace ocrl {
namespace {
void addNode(const vsgocct::cad::ShapeNode& node,
             const TopLoc_Location& parentLoc,
             BRep_Builder& builder,
             TopoDS_Compound& out)
{
    const TopLoc_Location loc = parentLoc * node.location;
    if (node.type == vsgocct::cad::ShapeNodeType::Part && !node.shape.IsNull())
        builder.Add(out, node.shape.Moved(loc));
    for (const auto& child : node.children)
        addNode(child, loc, builder, out);
}
} // namespace

TopoDS_Compound loadCompound(const std::filesystem::path& stepFile)
{
    const vsgocct::cad::AssemblyData assembly = vsgocct::cad::readStep(stepFile);
    TopoDS_Compound out;
    BRep_Builder builder;
    builder.MakeCompound(out);
    for (const auto& root : assembly.roots)
        addNode(root, TopLoc_Location(), builder, out);
    return out;
}
} // namespace ocrl
