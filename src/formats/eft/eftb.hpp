#pragma once

#include <string_view>

#include "formats/format.hpp"

// PTCL: sead::ptcl, nw::eft and nn::vfx particle effect binaries, signed
// SPBD (3DS), EFTB (Wii U era header) or VFXB (nn::util::BinaryFileHeader).
namespace nnfck::eft {

inline constexpr std::string_view Name = "Effect Binary";
inline constexpr std::string_view Abbreviation = "PTCL";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

void analyze_spbd(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::eft
