#pragma once

#include <string_view>

#include "formats/format.hpp"

// g3d shader archives (.bfsha): shading models, each with its options,
// attributes, samplers, uniform blocks and shader programs. Wii U (nw::g3d)
// files are big-endian with self-relative offsets and GX2 shaders; Switch
// (nn::g3d) files use an nn::util::BinaryFileHeader with absolute offsets,
// and each shading model embeds an nn::gfx shader file.
namespace nnfck::fsha {

inline constexpr std::string_view Name = "Binary Cafe Shader Archive";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::fsha
