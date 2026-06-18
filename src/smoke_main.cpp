#include <iostream>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <Standard_Version.hxx>
#include <vsg/core/Version.h>

int main()
{
    TopoDS_Compound c; BRep_Builder b; b.MakeCompound(c);
    std::cout << "OCCT " << OCC_VERSION_COMPLETE
              << ", VSG " << VSG_VERSION_STRING
              << ", compound null=" << c.IsNull() << "\n";
    return 0;
}
