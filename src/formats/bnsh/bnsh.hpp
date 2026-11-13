#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "formats/format.hpp"

// nn::gfx shader files: an nn::util::BinaryFileHeader and a shader container
// ("grsc") holding shader variations, each with up to a source, intermediate
// and binary program.
namespace nnfck::bnsh {

inline constexpr std::string_view Name = "Binary Nintendo Shader";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

// Shader stages in program order: vertex, hull, domain, geometry, pixel,
// compute.
inline constexpr std::size_t StageCount = 6;

struct Summary {
  std::string version;
  u16 api;
  u16 api_version;
  u8 code_type;
  u32 compiler_version;
  u64 low_level_compiler_version;
  // From the low-level compiler version of NVN containers.
  std::optional<std::string_view> platform;
  u32 file_size;
  u32 variations;
  // Variations holding a source, intermediate and binary program.
  u32 source = 0, intermediate = 0, binary = 0;
  // Programs with code for each stage.
  std::array<u32, StageCount> stages{};
};

// Reads the shader file that starts `data`, as embedded in other resources.
Summary summarize(Bytes data, const Context& ctx);

// "240 variations (240 binary)".
std::string variations_text(const Summary& s);
// "NVN, 240 variations (240 binary)".
std::string describe(const Summary& s);

}  // namespace nnfck::bnsh
