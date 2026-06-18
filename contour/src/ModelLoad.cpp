#include <occtcontour/ModelLoad.h>

#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Shape.hxx>

#include <fstream>
#include <stdexcept>
#include <string>

namespace ocrl {
TopoDS_Compound loadCompound(const std::filesystem::path& stepFile)
{
    std::ifstream input(stepFile, std::ios::binary);  // ifstream 兼容中文/Unicode 路径
    if (!input)
        throw std::runtime_error("cannot open STEP: " + stepFile.u8string());

    STEPControl_Reader reader;
    const std::string name = stepFile.filename().u8string();  // 绑定具名变量,避免 c_str() 悬垂
    if (reader.ReadStream(name.c_str(), input) != IFSelect_RetDone)
        throw std::runtime_error("OCCT failed to read STEP: " + stepFile.u8string());
    reader.TransferRoots();

    TopoDS_Compound out;
    BRep_Builder builder;
    builder.MakeCompound(out);
    for (int i = 1; i <= reader.NbShapes(); ++i) {
        const TopoDS_Shape s = reader.Shape(i);
        if (!s.IsNull()) builder.Add(out, s);
    }
    return out;
}
} // namespace ocrl
