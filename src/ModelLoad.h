#pragma once
#include <filesystem>
#include <TopoDS_Compound.hxx>

namespace ocrl {
// 读 STEP(复用 vsgocct::cad::readStep),递归拍平装配树为一个已应用 location 的 TopoDS_Compound。
TopoDS_Compound loadCompound(const std::filesystem::path& stepFile);
}
