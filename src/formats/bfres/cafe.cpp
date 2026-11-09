#include "formats/bfres/common.hpp"
#include "formats/gx2/gx2.hpp"

// Wii U (nw::g3d) resources: big-endian blocks that open with a 4-byte
// signature and refer to each other through self-relative 32-bit offsets.
namespace nnfck::bfres {

namespace {

constexpr std::size_t VertexStride = 0x20;
constexpr u32 VisibilityTypeShift = 8;

u64 cafe_offset(const ByteReader& r, std::size_t field) {
  const s32 v = r.s32_at(field);
  return v == 0 ? 0 : static_cast<u64>(static_cast<std::int64_t>(field) + v);
}

std::string cafe_string(const ByteReader& r, std::size_t field) {
  const u64 at = cafe_offset(r, field);
  return at != 0 ? r.cstring_at(static_cast<std::size_t>(at), MaxNameLength) : std::string();
}

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

std::size_t dict_count(const ByteReader& r, std::size_t field) {
  const u64 dict = cafe_offset(r, field);
  return dict == 0 ? 0 : static_cast<std::size_t>(r.s32_at(static_cast<std::size_t>(dict) + 4));
}

void expect_count(Analysis& out, const std::string& owner, const char* what, u32 declared, std::size_t listed) {
  if (declared != listed)
    out.warnings.push_back("BFRES " + owner + " counts " + std::to_string(declared) + " " + what +
                           " but its index group lists " + std::to_string(listed));
}

std::string model(const ByteReader& r, Analysis& out, std::size_t at, const std::string& name,
                  const Context& ctx) {
  const u16 vertex_count = r.u16_at(at + 0x20);
  const u16 shape_count = r.u16_at(at + 0x22);
  const u16 material_count = r.u16_at(at + 0x24);
  expect_count(out, "model \"" + name + "\"", "shapes", shape_count, dict_count(r, at + 0x14));
  expect_count(out, "model \"" + name + "\"", "materials", material_count, dict_count(r, at + 0x18));

  std::vector<std::string> parts;
  const u64 skeleton = cafe_offset(r, at + 0x0C);
  if (skeleton != 0 && r.magic_at(static_cast<std::size_t>(skeleton), "FSKL"))
    parts.push_back(count_of(r.u16_at(static_cast<std::size_t>(skeleton) + 0x08), "bone"));
  else
    out.warnings.push_back("BFRES model \"" + name + "\" has no FSKL skeleton block");
  parts.push_back(count_of(shape_count, "shape"));
  parts.push_back(count_of(material_count, "material"));

  const u64 vertices = cafe_offset(r, at + 0x10);
  // Models may share one vertex buffer array, so each walk is charged.
  ctx.spend(u64{vertex_count} * VertexStride, "vertex buffers");
  u64 total = 0;
  for (u16 i = 0; i < vertex_count && vertices != 0; ++i) {
    const std::size_t v = static_cast<std::size_t>(vertices) + std::size_t(i) * VertexStride;
    if (!r.magic_at(v, "FVTX")) {
      out.warnings.push_back("BFRES model \"" + name + "\" vertex buffer " + std::to_string(i) +
                             " lacks its FVTX signature");
      break;
    }
    total += r.u32_at(v + 0x08);
  }
  parts.push_back(count_of(vertex_count, "vertex buffer"));
  parts.push_back(count_of(total, "vertex", "vertices"));
  return join(parts);
}

// FTEX embeds a GX2Texture after its signature: the GX2Surface first, then
// the view range, channel selection and texture registers.
std::string texture(const ByteReader& r, std::size_t at) {
  const u32 dim = r.u32_at(at + 0x04);
  const u32 width = r.u32_at(at + 0x08);
  const u32 height = r.u32_at(at + 0x0C);
  const u32 depth = r.u32_at(at + 0x10);
  const u32 mips = r.u32_at(at + 0x14);
  const u32 format = r.u32_at(at + 0x18);
  const u32 aa = r.u32_at(at + 0x1C);
  const u32 tile = r.u32_at(at + 0x34);

  std::string size = std::to_string(width) + "x" + std::to_string(height);
  if (depth > 1)
    size += "x" + std::to_string(depth);
  std::vector<std::string> parts{size, gx2::surface_format_name(format), count_of(mips == 0 ? 1 : mips, "mip"),
                                 gx2::surface_dim_name(dim), gx2::tile_mode_name(tile)};
  if (aa != 0)
    parts.push_back(gx2::aa_mode_name(aa) + " AA");
  return join(parts);
}

std::string frames(s32 n) {
  return count_of(static_cast<u64>(n < 0 ? 0 : n), "frame");
}

// Before 3.4, frame counts are 16-bit and most element counts are narrower,
// which shifts every later field.
std::string skeletal_anim(const ByteReader& r, std::size_t at, bool legacy) {
  const s32 frame_count = legacy ? r.u16_at(at + 0x10) : r.s32_at(at + 0x10);
  const u16 bone_anims = r.u16_at(at + (legacy ? 0x12 : 0x14));
  const s32 curves = r.s32_at(at + (legacy ? 0x14 : 0x18));
  std::vector<std::string> parts{frames(frame_count), count_of(bone_anims, "bone animation"),
                                 count_of(static_cast<u32>(curves), "curve")};
  add_anim_flags(parts, r.u32_at(at + 0x0C));
  return join(parts);
}

std::string shader_param_anim(const ByteReader& r, std::size_t at, bool legacy) {
  const s32 frame_count = legacy ? r.u16_at(at + 0x10) : r.s32_at(at + 0x10);
  const u16 mat_anims = r.u16_at(at + (legacy ? 0x12 : 0x14));
  const s32 param_anims = r.s32_at(at + (legacy ? 0x14 : 0x18));
  const s32 curves = r.s32_at(at + (legacy ? 0x18 : 0x1C));
  std::vector<std::string> parts{frames(frame_count), count_of(mat_anims, "material animation"),
                                 count_of(static_cast<u32>(param_anims), "parameter animation"),
                                 count_of(static_cast<u32>(curves), "curve")};
  add_anim_flags(parts, r.u32_at(at + 0x0C));
  return join(parts);
}

std::string tex_pattern_anim(const ByteReader& r, std::size_t at, bool legacy) {
  const s32 frame_count = legacy ? r.u16_at(at + 0x0E) : r.s32_at(at + 0x10);
  const std::size_t counts = at + (legacy ? 0x10 : 0x14);
  std::vector<std::string> parts{frames(frame_count), count_of(r.u16_at(counts + 2), "material animation"),
                                 count_of(static_cast<u32>(r.s32_at(counts + 4)), "pattern animation"),
                                 count_of(r.u16_at(counts), "texture"),
                                 count_of(static_cast<u32>(r.s32_at(counts + 8)), "curve")};
  add_anim_flags(parts, r.u16_at(at + 0x0C));
  return join(parts);
}

std::string visibility_anim(const ByteReader& r, Analysis& out, std::size_t at, bool material,
                            const std::string& name, bool legacy) {
  const u16 flag = r.u16_at(at + 0x0C);
  if (((flag >> VisibilityTypeShift) & 1) != (material ? 1 : 0))
    out.warnings.push_back("BFRES visibility animation \"" + name + "\" is flagged as " +
                           (material ? "bone" : "material") + " visibility but listed as " +
                           (material ? "material" : "bone") + " visibility");
  const s32 frame_count = legacy ? r.u16_at(at + 0x0E) : r.s32_at(at + 0x10);
  const std::size_t counts = at + (legacy ? 0x10 : 0x14);
  std::vector<std::string> parts{frames(frame_count), count_of(r.u16_at(counts), "animation"),
                                 count_of(r.u16_at(counts + 2), "curve")};
  add_anim_flags(parts, flag);
  return join(parts);
}

std::string shape_anim(const ByteReader& r, std::size_t at, bool legacy) {
  std::vector<std::string> parts;
  if (legacy)
    parts = {frames(r.u16_at(at + 0x0E)), count_of(r.u16_at(at + 0x10), "vertex shape animation"),
             count_of(r.u8_at(at + 0x12), "key shape animation"), count_of(r.u8_at(at + 0x13), "curve")};
  else
    parts = {frames(r.s32_at(at + 0x10)), count_of(r.u16_at(at + 0x14), "vertex shape animation"),
             count_of(r.u16_at(at + 0x16), "key shape animation"), count_of(r.u16_at(at + 0x18), "curve")};
  add_anim_flags(parts, r.u16_at(at + 0x0C));
  return join(parts);
}

std::string scene_anim(const ByteReader& r, Analysis& out, std::size_t at, const std::string& name, bool legacy) {
  if (!legacy)
    return join({count_of(r.u16_at(at + 0x0E), "camera animation"), count_of(r.u16_at(at + 0x10), "light animation"),
                 count_of(r.u16_at(at + 0x12), "fog animation")});
  // Legacy scenes keep 8-bit counts ahead of the camera, light and fog
  // index groups; each count must agree with its group.
  const char* const kinds[] = {"camera", "light", "fog"};
  std::vector<std::string> parts;
  for (std::size_t i = 0; i < 3; ++i) {
    const u8 declared = r.u8_at(at + 0x0E + i);
    const std::size_t listed = dict_count(r, at + 0x14 + i * 4);
    if (declared != listed)
      out.warnings.push_back("BFRES scene animation \"" + name + "\" counts " + std::to_string(declared) + " " +
                             kinds[i] + " animations but its index group lists " + std::to_string(listed));
    parts.push_back(count_of(declared, std::string(kinds[i]) + " animation"));
  }
  return join(parts);
}

enum class Kind { Model, Texture, Skeletal, ShaderParam, TexPattern, BoneVis, MatVis, Shape, Scene };

std::string describe(const ByteReader& r, Analysis& out, Kind kind, std::size_t at, const std::string& name,
                     bool legacy, const Context& ctx) {
  switch (kind) {
  case Kind::Model: return model(r, out, at, name, ctx);
  case Kind::Texture: return texture(r, at);
  case Kind::Skeletal: return skeletal_anim(r, at, legacy);
  case Kind::ShaderParam: return shader_param_anim(r, at, legacy);
  case Kind::TexPattern: return tex_pattern_anim(r, at, legacy);
  case Kind::BoneVis: return visibility_anim(r, out, at, false, name, legacy);
  case Kind::MatVis: return visibility_anim(r, out, at, true, name, legacy);
  case Kind::Shape: return shape_anim(r, at, legacy);
  case Kind::Scene: return scene_anim(r, out, at, name, legacy);
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
    const char* label;
    const char* magic;
    Kind kind;
  };
  constexpr std::size_t scene_field = 0x48;
  const CafeGroup layout[] = {
      {0x20, "Models (FMDL)", "FMDL", Kind::Model},
      {0x24, "Textures (FTEX)", "FTEX", Kind::Texture},
      {0x28, "Skeletal Animations (FSKA)", "FSKA", Kind::Skeletal},
      {0x2C, "Shader Parameter Animations (FSHU)", "FSHU", Kind::ShaderParam},
      {0x30, "Color Animations (FSHU)", "FSHU", Kind::ShaderParam},
      {0x34, "Texture SRT Animations (FSHU)", "FSHU", Kind::ShaderParam},
      {0x38, "Texture Pattern Animations (FTXP)", "FTXP", Kind::TexPattern},
      {0x3C, "Bone Visibility Animations (FVIS)", "FVIS", Kind::BoneVis},
      {0x40, "Material Visibility Animations (FVIS)", "FVIS", Kind::MatVis},
      {0x44, "Shape Animations (FSHA)", "FSHA", Kind::Shape},
      {scene_field, "Scene Animations (FSCN)", "FSCN", Kind::Scene},
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

}  // namespace nnfck::bfres
