#include "formats/bnsh/bnsh.hpp"

#include <vector>

#include "formats/byte_order.hpp"
#include "formats/gfx/gfx.hpp"
#include "formats/listing.hpp"

namespace nnfck::bnsh {

namespace {

using listing::count_of;

constexpr std::size_t HeaderSize = 0x20;
constexpr std::size_t ContainerSize = 0x60;
constexpr std::size_t VariationSize = 0x40;
constexpr std::size_t ProgramSize = 0xA0;
// A program opens with its shader info: flags, code type, source format, then
// one code pointer per stage from here.
constexpr std::size_t StageCodeField = 0x8;
constexpr std::size_t ProgramParentField = 0x70;
constexpr u16 NvnApi = 4;
constexpr std::size_t MaxNameLength = 0x400;

constexpr const char* Stages[StageCount] = {"vertex", "hull", "domain", "geometry", "pixel", "compute"};

std::string version_string(u32 v) {
  return std::to_string(v >> 16) + "." + std::to_string((v >> 8) & 0xFF) + "." + std::to_string(v & 0xFF);
}

// Variations hold source, intermediate and binary program pointers, in that
// order, each program pointing back at its variation.
void read_programs(const ByteReader& r, Summary& s, std::size_t variations, const Context& ctx) {
  r.require(variations, std::size_t{s.variations} * VariationSize, "BNSH shader variations");
  ctx.spend(u64{s.variations} * VariationSize, "shader variations");
  u32* const kinds[] = {&s.source, &s.intermediate, &s.binary};
  for (u32 i = 0; i < s.variations; ++i) {
    const std::size_t v = variations + std::size_t{i} * VariationSize;
    for (std::size_t k = 0; k < 3; ++k) {
      const u64 p = r.u64_at(v + k * 8);
      if (p == 0)
        continue;
      r.require(static_cast<std::size_t>(p), ProgramSize, "BNSH shader program");
      if (r.u64_at(static_cast<std::size_t>(p) + ProgramParentField) != v)
        throw ParseError("shader program at " + fmt_hex(p) + " belongs to another variation");
      ++*kinds[k];
      for (std::size_t st = 0; st < StageCount; ++st)
        if (r.u64_at(static_cast<std::size_t>(p) + StageCodeField + st * 8) != 0)
          ++s.stages[st];
    }
  }
}

}  // namespace

bool probe(Bytes data) {
  return ByteReader(data, Endian::Big).magic_at(0, std::string_view("BNSH\0\0\0\0", 8));
}

Summary summarize(Bytes data, const Context& ctx) {
  ByteReader r(data, Endian::Little);
  r.require(0, HeaderSize, "BNSH header");
  r.set_endian(read_bom(r, 0xC));
  Summary s{};
  s.version = version_string(r.u32_at(0x8));
  s.file_size = r.u32_at(0x1C);
  const std::size_t container = r.u16_at(0x16);
  r.require(container, ContainerSize, "BNSH shader container");
  if (!r.magic_at(container, "grsc"))
    throw ParseError("BNSH first block at " + fmt_hex(container) + " is not a shader container");
  s.api = r.u16_at(container + 0x10);
  s.api_version = r.u16_at(container + 0x12);
  s.code_type = r.u8_at(container + 0x14);
  s.compiler_version = r.u32_at(container + 0x18);
  s.variations = r.u32_at(container + 0x1C);
  s.low_level_compiler_version = r.u64_at(container + 0x30);
  if (s.api == NvnApi)
    s.platform = gfx::nvn_platform(static_cast<u16>(s.low_level_compiler_version >> 16));
  read_programs(r, s, static_cast<std::size_t>(r.u64_at(container + 0x20)), ctx);
  return s;
}

std::string variations_text(const Summary& s) {
  std::vector<std::string> kinds;
  if (s.binary != 0)
    kinds.push_back(std::to_string(s.binary) + " binary");
  if (s.intermediate != 0)
    kinds.push_back(std::to_string(s.intermediate) + " intermediate");
  if (s.source != 0)
    kinds.push_back(std::to_string(s.source) + " source");
  return count_of(s.variations, "variation") + (kinds.empty() ? "" : " (" + listing::join(kinds) + ")");
}

std::string describe(const Summary& s) {
  return gfx::low_level_api_name(s.api) + ", " + variations_text(s);
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(Name);
  const Summary s = summarize(data, ctx);
  if (s.platform)
    out.format += " (" + std::string(*s.platform) + ")";
  ByteReader r(data, read_bom(ByteReader(data, Endian::Little), 0xC));

  Section& sec = out.section(std::string(Name));
  if (s.platform)
    sec.add("Platform", std::string(*s.platform));
  sec.add("Version", s.version);
  sec.add("Byte Order", std::string(endian_name(r.endian())));
  const u8 alignment_shift = r.u8_at(0xE);
  sec.add("Alignment", alignment_shift < 32 ? fmt_hex(u64{1} << alignment_shift)
                                            : "Invalid (2^" + std::to_string(alignment_shift) + ")");
  sec.add("File Size", fmt_size(s.file_size));
  if (const u32 name = r.u32_at(0x10); name != 0)
    sec.add("Name", r.cstring_at(name, MaxNameLength));
  sec.add("Target API", gfx::low_level_api_name(s.api) + " (version " + std::to_string(s.api_version) + ")");
  sec.add("Code Type", gfx::shader_code_type_name(s.code_type));
  sec.add("Compiler Version", fmt_hex(s.compiler_version));
  sec.add("Low-Level Compiler Version", fmt_hex(s.low_level_compiler_version, 16));
  sec.add("Shader Variations", variations_text(s));

  std::vector<std::string> stages;
  for (std::size_t st = 0; st < StageCount; ++st)
    if (s.stages[st] != 0)
      stages.push_back(std::to_string(s.stages[st]) + " " + Stages[st]);
  if (!stages.empty())
    sec.add("Stage Code", listing::join(stages));

  if (s.file_size != data.size())
    out.warnings.push_back("BNSH declares " + fmt_hex(s.file_size) + " bytes but holds " + fmt_hex(data.size()));
  if (const u32 reloc = r.u32_at(0x18); reloc != 0 && !r.magic_at(reloc, "_RLT"))
    out.warnings.push_back("BNSH relocation table at " + fmt_hex(reloc) + " lacks its _RLT signature");
}

}  // namespace nnfck::bnsh
