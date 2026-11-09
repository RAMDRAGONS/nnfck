#pragma once

#include <string_view>

#include "formats/format.hpp"

// PTCL: nn::vfx particle effect binaries, signed EFTB (Wii U era header) or
// VFXB (nn::util::BinaryFileHeader).
namespace nnfck::eft {

inline constexpr std::string_view Name = "NintendoSDK Effect Binary";
inline constexpr std::string_view Abbreviation = "PTCL";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::eft
