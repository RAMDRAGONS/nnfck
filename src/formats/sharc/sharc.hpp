#pragma once

#include <string_view>

#include "formats/format.hpp"

// agl shader archives. Both kinds store their signature little-endian, so
// files begin with the bytes "AAHS" or "BAHS", and nest their tables as
// arrays that open with their byte size and entry count, each entry opening
// with its own size.
namespace nnfck::sharc {

// agl::ResShaderArchive (signature SHAA, .sharc): shader programs, each with
// its macros, variations and symbols, and the source files they build from.
inline constexpr std::string_view Name = "Shader Archive";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

// agl::ResBinaryShaderArchive (signature SHAB): compiled shaders and the
// programs that use them, .sharcfb on Wii U (versions 8 and 9) and .sharcb on
// Switch and Switch 2, where the header grows to hold the NVN objects for the
// shader memory pool.
inline constexpr std::string_view BinaryName = "Binary Shader Archive";

bool probe_binary(Bytes data);
void analyze_binary(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::sharc
