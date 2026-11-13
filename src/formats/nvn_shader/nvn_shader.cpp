#include "formats/nvn_shader/nvn_shader.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "formats/gfx/gfx.hpp"

namespace nnfck::nvn_shader {

namespace {

// Each stage takes a control offset and a code offset.
constexpr std::size_t MaxStages = 6;
constexpr u32 CodeMagic = 0x12345678;
// Code opens with a 0x30-byte header, then the Maxwell shader program
// header, whose first word gives its kind and stage.
constexpr std::size_t ProgramHeaderField = 0x30;
constexpr u32 PixelProgram = 2;

constexpr const char* VertexStages[] = {"vertex (cull before fetch)", "vertex", "tessellation control",
                                        "tessellation evaluation", "geometry"};

std::string stage_name(const ByteReader& r, std::size_t code) {
  if (!r.has(code + ProgramHeaderField, 4) || r.u32_at(code) != CodeMagic)
    return "unknown stage";
  const u32 sph = r.u32_at(code + ProgramHeaderField);
  const u32 kind = sph & 0x1F;
  const u32 type = (sph >> 10) & 0xF;
  if (kind == PixelProgram)
    return "fragment";
  if (kind == 1 && type < std::size(VertexStages))
    return VertexStages[type];
  return "unknown stage";
}

// The header is twice as many words as there are stages, the first control
// section starting right after it.
std::optional<std::size_t> stage_count(const ByteReader& r) {
  if (!r.has(0, 8))
    return std::nullopt;
  const u32 header = r.u32_at(0);
  if (header % 8 != 0 || header == 0 || header / 8 > MaxStages || !r.has(header, 8) ||
      r.u32_at(header) != gfx::NvnControlMagic)
    return std::nullopt;
  return header / 8;
}

}  // namespace

bool probe(Bytes data) {
  return stage_count(ByteReader(data, Endian::Little)).has_value();
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(Name);
  const ByteReader r(data, Endian::Little);
  const std::size_t stages = *stage_count(r);

  std::vector<u32> offsets;
  for (std::size_t i = 0; i < stages * 2; ++i)
    offsets.push_back(r.u32_at(i * 4));
  for (const u32 o : offsets)
    if (o >= data.size())
      throw ParseError("section at " + fmt_hex(o) + " lies outside the file");
  // Sections run up to the next one in the file, the last to its end.
  std::vector<u32> sorted = offsets;
  std::sort(sorted.begin(), sorted.end());
  const auto span = [&](u32 at) {
    const auto next = std::upper_bound(sorted.begin(), sorted.end(), at);
    return (next == sorted.end() ? static_cast<u32>(data.size()) : *next) - at;
  };

  const u32 control_version = r.u32_at(offsets[0] + 4);
  const std::optional<std::string_view> platform = gfx::nvn_platform(control_version);
  if (platform)
    out.format += " (" + std::string(*platform) + ")";

  Section& s = out.section(std::string(Name));
  if (platform)
    s.add("Platform", std::string(*platform));
  s.add("Control Version", std::to_string(control_version));
  s.add("Stages", std::to_string(stages));
  for (std::size_t i = 0; i < stages; ++i) {
    const u32 control = offsets[i], code = offsets[stages + i];
    if (!r.has(control, 8) || r.u32_at(control) != gfx::NvnControlMagic)
      out.errors.push_back("NVN shader stage " + std::to_string(i) + " control section at " + fmt_hex(control) +
                           " lacks its signature");
    else if (r.u32_at(control + 4) != control_version)
      out.warnings.push_back("NVN shader stage " + std::to_string(i) + " has control version " +
                             std::to_string(r.u32_at(control + 4)));
    ctx.spend(0x40, "shader stages");
    s.add(stage_name(r, code), "control " + fmt_hex(span(control)) + " bytes at " + fmt_hex(control) + ", code " +
                                   fmt_hex(span(code)) + " bytes at " + fmt_hex(code),
          1);
  }
}

}  // namespace nnfck::nvn_shader
