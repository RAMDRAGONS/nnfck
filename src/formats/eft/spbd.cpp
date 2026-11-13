#include <array>
#include <string>
#include <vector>

#include "core/shift_jis.hpp"
#include "formats/eft/eftb.hpp"
#include "formats/listing.hpp"

namespace nnfck::eft {

namespace {

constexpr std::size_t EmitterSetSize = 0x1C;
constexpr std::size_t EmitterRefSize = 0x8;
constexpr std::size_t MaxNameLength = 0x100;

constexpr std::size_t TypeCount = 3;

// Version 11 files come from sead::ptcl and version 51 from nw::eft, sharing
// the emitter set records. Version 51 grows the header with shader, animation
// key, primitive and material command tables, and moves each emitter's name
// further in.
struct Layout {
  u32 version;
  std::string_view library;
  std::size_t header;
  std::size_t emitter_name;
  bool tables;
  std::array<const char*, TypeCount> types;
};

constexpr Layout Layouts[] = {
    {11, library::Sead, 0x1C, 0x0C, false, {"simple", "complex", "simple GPU"}},
    {51, library::NintendoWare, 0x48, 0x38, true, {"simple", "complex", "compact"}},
};

}  // namespace

// Header: emitter set count, then name table, texture table and its size,
// all positions from the start of the file. Emitter set records follow the
// header, each naming its emitters through a table of file positions.
void analyze_spbd(Bytes data, Analysis& out, const Context& ctx) {
  const ByteReader r(data, Endian::Little);
  r.require(0, 0x1C, "SPBD header");
  const u32 version = r.u32_at(0x04);
  out.format = std::string(Name) + " (" + std::string(Abbreviation) + ", 3DS)";

  Section& s = out.section(std::string(Name) + " (" + std::string(Abbreviation) + ")");
  s.add("Signature", "SPBD");
  s.add("Platform", "3DS");
  s.add("Binary Version", std::to_string(version));

  const Layout* l = nullptr;
  for (const Layout& candidate : Layouts)
    if (candidate.version == version)
      l = &candidate;
  if (l == nullptr) {
    out.warnings.push_back("SPBD version " + std::to_string(version) + " has an unknown layout; only its version is shown");
    return;
  }
  out.library = std::string(l->library);
  r.require(0, l->header, "SPBD header");
  const u32 set_count = r.u32_at(0x08);
  const std::size_t names = r.u32_at(0x10);
  const u32 textures = r.u32_at(0x14);
  const u32 texture_size = r.u32_at(0x18);
  const auto name_at = [&](u32 pos) {
    return shift_jis_to_utf8(r.cstring_at(names + pos, MaxNameLength));
  };

  s.add("Name", name_at(r.u32_at(0x0C)));
  if (u64{textures} + texture_size > data.size())
    out.errors.push_back("SPBD texture table at " + fmt_hex(textures) + " runs " + fmt_hex(texture_size) +
                         " bytes past the end of the file");
  r.require(l->header, std::size_t{set_count} * EmitterSetSize, "SPBD emitter sets");

  std::array<u64, TypeCount + 1> types{};
  u64 emitters = 0;
  std::vector<std::string> rows;
  for (u32 i = 0; i < set_count; ++i) {
    const std::size_t set = l->header + std::size_t{i} * EmitterSetSize;
    std::string row = name_at(r.u32_at(set + 0x8));
    const u32 count = r.u32_at(set + 0x10);
    const std::size_t table = r.u32_at(set + 0x14);
    ctx.spend(EmitterSetSize + row.size(), "emitter sets");
    if (table == 0 || count == 0) {
      if (ctx.list_members)
        rows.push_back(row + ": no emitters");
      continue;
    }
    r.require(table, std::size_t{count} * EmitterRefSize, "SPBD emitter table");
    // Sets may share one table, so every visit is charged.
    ctx.spend(std::size_t{count} * EmitterRefSize, "emitter tables");
    std::vector<std::string> emitter_names;
    for (u32 k = 0; k < count; ++k) {
      const std::size_t emitter = r.u32_at(table + std::size_t{k} * EmitterRefSize);
      // A zero position leaves the slot empty.
      if (emitter == 0)
        continue;
      r.require(emitter, l->emitter_name + 4, "SPBD emitter");
      const u32 type = r.u32_at(emitter);
      ++types[type < TypeCount ? type : TypeCount];
      ++emitters;
      if (ctx.list_members) {
        emitter_names.push_back(name_at(r.u32_at(emitter + l->emitter_name)));
        ctx.spend(emitter_names.back().size(), "emitter names");
      }
    }
    if (ctx.list_members)
      rows.push_back(row + ": " + (emitter_names.empty() ? "no emitters" : listing::join(emitter_names)));
  }

  s.add("Emitter Sets", std::to_string(set_count));
  std::vector<std::string> kinds;
  for (std::size_t t = 0; t < TypeCount; ++t)
    if (types[t] != 0)
      kinds.push_back(std::to_string(types[t]) + " " + l->types[t]);
  if (types.back() != 0)
    kinds.push_back(std::to_string(types.back()) + " of unknown type");
  s.add("Emitters", std::to_string(emitters) + (kinds.empty() ? "" : " (" + listing::join(kinds) + ")"));
  if (l->tables) {
    // Shader and primitive tables each open with their entry count.
    const auto table_count = [&](std::size_t field) {
      const u32 pos = r.u32_at(field);
      return pos == 0 ? std::string("None") : std::to_string(r.u32_at(pos));
    };
    s.add("Shaders", table_count(0x1C));
    s.add("Primitives", table_count(0x2C));
  }
  s.add("Texture Data", fmt_size(texture_size));

  if (!ctx.list_members || rows.empty())
    return;
  Section& list = out.section(std::string(Name) + " Emitter Sets");
  for (const std::string& row : rows)
    list.add("", row);
}

}  // namespace nnfck::eft
