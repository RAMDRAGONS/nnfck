#include "formats/fres/resources.hpp"

// Wii U (nw::g3d) resources: big-endian blocks that open with a 4-byte
// signature and refer to each other through self-relative 32-bit offsets.
namespace nnfck::fres {

namespace {

// Index groups: node 0 is the root of the lookup tree; nodes 1..count hold
// the entries.
std::vector<ListEntry> cafe_dict(const ByteReader& r, std::size_t field, const Context& ctx) {
  std::vector<ListEntry> entries;
  const u64 dict = cafe_offset(r, field);
  if (dict == 0)
    return entries;
  const s32 count = r.s32_at(static_cast<std::size_t>(dict) + 4);
  if (count < 0 || static_cast<std::size_t>(count) > max_dict_entries(r, static_cast<std::size_t>(dict)))
    throw ParseError("index group at " + fmt_hex(dict) + " claims " + std::to_string(count) + " entries");
  for (s32 i = 1; i <= count; ++i) {
    const std::size_t node = static_cast<std::size_t>(dict) + 8 + static_cast<std::size_t>(i) * DictNodeSize;
    entries.push_back({cafe_string(r, node + 8), cafe_offset(r, node + 12), {}});
    ctx.spend(DictNodeSize + entries.back().name.size(), "index group names");
  }
  return entries;
}

enum class Kind { Model, Texture, Skeletal, ShaderParam, TexPattern, BoneVis, MatVis, Shape, Scene };

std::string describe(const ByteReader& r, Analysis& out, Kind kind, std::size_t at, const std::string& name,
                     bool legacy, const Context& ctx) {
  switch (kind) {
  case Kind::Model: return fmdl::describe_cafe(r, out, at, name, ctx);
  case Kind::Texture: return ftex::describe_cafe(r, at);
  case Kind::Skeletal: return fska::describe_cafe(r, at, legacy);
  case Kind::ShaderParam: return fshu::describe_cafe(r, at, legacy);
  case Kind::TexPattern: return ftxp::describe_cafe(r, at, legacy);
  case Kind::BoneVis: return fvis::describe_cafe(r, out, at, false, name, legacy);
  case Kind::MatVis: return fvis::describe_cafe(r, out, at, true, name, legacy);
  case Kind::Shape: return fsha::describe_cafe(r, at, legacy);
  case Kind::Scene: return fscn::describe_cafe(r, out, at, name, legacy);
  }
  return {};
}

// Every block's own name sits at the same offset relative to the block,
// except textures, where the GX2 texture plus a handle and array length
// precede it.
std::size_t name_field(Kind kind) {
  return kind == Kind::Texture ? 0xA8 : 0x04;
}

}  // namespace

// Four version bytes major.minor.micro.binaryBugfix, then a big-endian header
// whose index groups and counts follow in a fixed order.
void analyze_cafe(ByteReader& r, Analysis& out, const Context& ctx) {
  const Bytes v = r.data().subspan(0x4, 4);
  r.set_endian(read_bom(r, 0x8));
  const u16 header_size = r.u16_at(0xA);
  const u32 file_size = r.u32_at(0xC);
  const u32 alignment = r.u32_at(0x10);

  out.format = Title + " (Wii U)";
  Section& s = out.section(Title);
  s.add("Platform", "Wii U");
  s.add("Version", std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]) + "." +
                       std::to_string(v[3]));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Header Size", fmt_hex(header_size));
  s.add("File Size", fmt_size(file_size));
  s.add("Alignment", fmt_hex(alignment));
  if (const std::string name = cafe_string(r, 0x14); !name.empty())
    s.add("Name", name);

  check_file_size(out, file_size, r.size());
  if (v[0] > 4)
    out.warnings.push_back("Wii U BFRES version " + std::to_string(v[0]) + ".x is newer than the known 3.x to 4.x");
  // 2.x files lay out their index groups and blocks differently.
  if (v[0] < 3) {
    out.warnings.push_back("Wii U BFRES " + std::to_string(v[0]) + ".x layouts are unknown; contents are not listed");
    return;
  }

  struct CafeGroup {
    std::size_t field;
    std::string label;
    const char* magic;
    Kind kind;
  };
  constexpr std::size_t scene_field = 0x48;
  // Color and texture SRT animations are shader parameter animations that
  // the header indexes in groups of their own.
  const CafeGroup layout[] = {
      {0x20, std::string(fmdl::Name), "FMDL", Kind::Model},
      {0x24, std::string(ftex::Name), "FTEX", Kind::Texture},
      {0x28, std::string(fska::Name), "FSKA", Kind::Skeletal},
      {0x2C, std::string(fshu::Name), "FSHU", Kind::ShaderParam},
      {0x30, std::string(fshu::Name) + " (color)", "FSHU", Kind::ShaderParam},
      {0x34, std::string(fshu::Name) + " (texture SRT)", "FSHU", Kind::ShaderParam},
      {0x38, std::string(ftxp::Name), "FTXP", Kind::TexPattern},
      {0x3C, std::string(fvis::BoneName), "FVIS", Kind::BoneVis},
      {0x40, std::string(fvis::MaterialName), "FVIS", Kind::MatVis},
      {0x44, std::string(fsha::Name), "FSHA", Kind::Shape},
      {scene_field, std::string(fscn::Name), "FSCN", Kind::Scene},
  };

  // Per-type counts follow the twelve index groups in the same order.
  const auto check_count = [&](std::size_t field, const char* magic, std::size_t listed) {
    const u16 declared = r.u16_at(0x50 + (field - 0x20) / 2);
    if (declared != listed)
      out.warnings.push_back("BFRES header counts " + std::to_string(declared) + " " + magic +
                             " entries but its index group lists " + std::to_string(listed));
  };

  // 3.0 animation blocks store 16-bit frame counts and narrower element
  // counts. 3.1 to 3.3 may share that layout, but too few samples exist to
  // tell.
  const bool legacy_anims = v[0] == 3 && v[1] == 0;
  const bool anim_layout_known = v[0] > 3 || v[1] == 0 || v[1] >= 4;
  bool anims_skipped = false;

  std::vector<Group> groups;
  for (const CafeGroup& g : layout) {
    Group group{g.label, cafe_dict(r, g.field, ctx)};
    check_count(g.field, g.magic, group.entries.size());
    for (ListEntry& e : group.entries) {
      const std::size_t at = static_cast<std::size_t>(e.data);
      // 3.0 bone visibility blocks leave their signature zeroed.
      const bool unsigned_block = legacy_anims && g.kind == Kind::BoneVis && e.data != 0 &&
                                  at <= r.size() - 4 && r.u32_at(at) == 0;
      if (e.data == 0 || (!unsigned_block && !r.magic_at(at, g.magic))) {
        out.warnings.push_back("BFRES " + std::string(g.magic) + " entry \"" + e.name + "\" does not point at a " +
                               g.magic + " block");
        continue;
      }
      const std::string own = cafe_string(r, at + name_field(g.kind));
      ctx.spend(own.size(), "block names");
      if (own != e.name)
        out.warnings.push_back("BFRES " + std::string(g.magic) + " block named \"" + own + "\" is indexed as \"" +
                               e.name + "\"");
      if (!ctx.list_members)
        continue;
      if (anim_layout_known || g.kind == Kind::Model || g.kind == Kind::Texture)
        e.detail = describe(r, out, g.kind, at, e.name, legacy_anims, ctx);
      else
        anims_skipped = true;
    }
    groups.push_back(std::move(group));
  }
  if (anims_skipped)
    out.warnings.push_back("Wii U BFRES " + std::to_string(v[0]) + "." + std::to_string(v[1]) +
                           " animation layouts are unknown; animations are listed by name only");
  const std::vector<ListEntry> externals = cafe_dict(r, scene_field + 4, ctx);
  check_count(scene_field + 4, "external file", externals.size());
  if (!ctx.list_members)
    return;
  add_groups(out, groups);

  // Each external file entry points at a self-relative data offset and a size.
  std::vector<std::pair<u64, u64>> spans;
  for (const ListEntry& e : externals) {
    if (e.data == 0) {
      spans.emplace_back(0, 0);
      continue;
    }
    const std::size_t at = static_cast<std::size_t>(e.data);
    spans.emplace_back(cafe_offset(r, at), r.u32_at(at + 4));
  }
  add_external_files(out, externals, spans, r.data(), ctx);
}

}  // namespace nnfck::fres
