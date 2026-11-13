#pragma once

#include <string_view>

#include "formats/format.hpp"

// Bare NVN shader programs as sead's NVN debug font loads them: one control
// section offset per stage, then one code offset per stage, each section
// holding NVN shader compiler output as passed to nvnProgramSetShaders.
namespace nnfck::nvn_shader {

inline constexpr std::string_view Name = "NVN Shader";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::nvn_shader
