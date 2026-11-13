#pragma once

#include <string_view>

#include "formats/format.hpp"

// NVN texture packager archives (NvFD, .xtx; Splatoon 2 names them .ntx): an
// archive header, then blocks each opening with an NvBH header that gives its
// type and where its data sits. Texture info blocks describe a texture in
// NVN's own terms and image data blocks hold its pixels, ready for NVN.
namespace nnfck::xtx {

inline constexpr std::string_view Name = "NVN Texture";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::xtx
