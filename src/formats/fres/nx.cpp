#include <iterator>
#include <optional>

#include "formats/fres/resources.hpp"

// Switch (nn::g3d) resources: blocks referring to each other through absolute
// 64-bit offsets, stored as arrays alongside dictionaries that name them.
namespace nnfck::fres {

namespace {

constexpr NxLayout Layout5 = {
    0x10,
    0x78, 0x20, 0x28, 0x38, 0x48, 0x68, 0x6A, 0x6C,
    0x3C,
    0x60, 0x58,
    0x60, 0x48, 0x4C, 0x50, 0x58,
    0x78, 0x60, 0x68, 0x66, 0x64, 0x70, 0x72, 0x74, 0x76,
    0x68, 0x58, 0x5C, 0x60, 0x62,
    0x60, 0x48, 0x50, 0x4C, 0x4E, 0x58,
    0x68, 0x62, 0x64, 0x66,
};

// 8.x grew the skeleton by 16 bytes ahead of its counts; every other block
// matches 5.x.
constexpr NxLayout Layout8 = [] {
  NxLayout l = Layout5;
  l.skeleton_bone_count = 0x4C;
  return l;
}();

constexpr NxLayout Layout9 = {
    0x08,
    0x78, 0x18, 0x20, 0x30, 0x48, 0x68, 0x6A, 0x6C,
    0x38,
    0x58, 0x50,
    0x50, 0x04, 0x40, 0x44, 0x4C,
    0x70, 0x04, 0x58, 0x64, 0x62, 0x66, 0x68, 0x6A, 0x6C,
    0x60, 0x04, 0x50, 0x58, 0x5A,
    0x50, 0x04, 0x40, 0x4A, 0x4C, 0x4E,
    0x60, 0x5A, 0x5C, 0x5E,
};

// 10.x moved the model's material dictionary up into what 9.x reserved.
constexpr NxLayout Layout10 = [] {
  NxLayout l = Layout9;
  l.model_material_dic = 0x40;
  return l;
}();

std::optional<NxLayout> layout_for(u16 major) {
  switch (major) {
  case 5: return Layout5;
  case 8: return Layout8;
  case 9: return Layout9;
  case 10: return Layout10;
  default: return std::nullopt;
  }
}

std::vector<ListEntry> nx_dict(const ByteReader& r, Analysis& out, std::size_t field, const Context& ctx) {
  std::vector<ListEntry> entries;
  const u64 dict = r.u64_at(field);
  if (dict == 0)
    return entries;
  // Older files leave the signature zeroed; later ones write "_DIC".
  if (!r.magic_at(static_cast<std::size_t>(dict), "_DIC") && r.u32_at(static_cast<std::size_t>(dict)) != 0) {
    out.warnings.push_back("BFRES dictionary at " + fmt_hex(dict) + " has an unknown signature");
    return entries;
  }
  const s32 count = r.s32_at(static_cast<std::size_t>(dict) + 4);
  if (count < 0 || static_cast<std::size_t>(count) > max_dict_entries(r, static_cast<std::size_t>(dict)))
    throw ParseError("dictionary at " + fmt_hex(dict) + " claims " + std::to_string(count) + " entries");
  for (s32 i = 1; i <= count; ++i) {
    const std::size_t node = static_cast<std::size_t>(dict) + 8 + static_cast<std::size_t>(i) * DictNodeSize;
    entries.push_back({nx_string(r, r.u64_at(node + 8)), 0, {}});
    ctx.spend(DictNodeSize + entries.back().name.size(), "dictionary names");
  }
  return entries;
}

enum class Kind { Model, Skeletal, Material, BoneVis, Shape, Scene };

std::size_t stride(const NxLayout& l, Kind kind) {
  switch (kind) {
  case Kind::Model: return l.model_stride;
  case Kind::Skeletal: return l.skeletal_stride;
  case Kind::Material: return l.material_stride;
  case Kind::BoneVis: return l.bone_vis_stride;
  case Kind::Shape: return l.shape_stride;
  case Kind::Scene: return l.scene_stride;
  }
  return 0;
}

std::string describe(const ByteReader& r, const NxLayout& l, Analysis& out, Kind kind, std::size_t at,
                     const std::string& name, const Context& ctx) {
  switch (kind) {
  case Kind::Model: return fmdl::describe_nx(r, l, out, at, name, ctx);
  case Kind::Skeletal: return fska::describe_nx(r, l, at);
  case Kind::Material: return fmaa::describe_nx(r, l, at);
  case Kind::BoneVis: return fvis::describe_nx(r, l, at);
  case Kind::Shape: return fsha::describe_nx(r, l, at);
  case Kind::Scene: return fscn::describe_nx(r, l, at);
  }
  return {};
}

}  // namespace

// nn::util::BinaryFileHeader: an 8-byte "FRES    " signature, version as u8
// micro, u8 minor, u16 major, and absolute 64-bit offsets.
void analyze_nx(ByteReader& r, Analysis& out, const Context& ctx) {
  r.set_endian(read_bom(r, 0xC));
  const u32 version = r.u32_at(0x8);
  const u16 major = static_cast<u16>(version >> 16);
  const u8 minor = static_cast<u8>(version >> 8);
  const u8 micro = static_cast<u8>(version);
  const u8 alignment_shift = r.u8_at(0xE);
  const u8 address_size = r.u8_at(0xF);
  const u16 relocated = r.u16_at(0x14);
  const u16 first_block = r.u16_at(0x16);
  const u32 relocation_table = r.u32_at(0x18);
  const u32 file_size = r.u32_at(0x1C);

  out.format = Title + " (Switch)";
  Section& s = out.section(Title);
  s.add("Platform", "Switch");
  s.add("Version", std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(micro));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Alignment", alignment_shift < 32 ? fmt_hex(u64{1} << alignment_shift) : "Invalid (2^" +
                                                std::to_string(alignment_shift) + ")");
  s.add("Target Address Size", std::to_string(address_size));
  s.add("First Block Offset", fmt_hex(first_block));
  s.add("Relocation Table", fmt_hex(relocation_table));
  s.add("Relocated", fmt_bool(relocated != 0));
  s.add("File Size", fmt_size(file_size));
  if (const std::string name = nx_string(r, r.u64_at(0x20)); !name.empty())
    s.add("Name", name);

  if (relocation_table != 0 && !r.magic_at(relocation_table, "_RLT"))
    out.warnings.push_back("BFRES relocation table at " + fmt_hex(relocation_table) + " lacks its _RLT signature");

  if (major > 10 || major == 1 || major == 6 || major == 7)
    out.warnings.push_back("Switch BFRES version " + std::to_string(major) + ".x has no verified layout");

  // From 10.x a GPU buffer section may follow the declared file image, so only
  // a declared size beyond the data is a definite inconsistency.
  if (major >= 10) {
    if (file_size > r.size())
      out.warnings.push_back("BFRES declares " + fmt_hex(file_size) + " bytes but holds only " +
                             fmt_hex(r.size()));
  } else {
    check_file_size(out, file_size, r.size());
  }

  // 9.x inserted 32 bytes ahead of the animation dictionaries; before 1.x the
  // vertex and index buffers had separate memory pools, shifting what follows.
  const std::size_t o = 0x38 + (major >= 9 ? 0x20 : 0);
  const std::size_t pool_shift = major < 1 ? 0x10 : 0;

  // Per-type counts follow the string pool size: the model count, then (with
  // four reserved bytes from 9.x) the animation and external file counts.
  const std::size_t counts = o + 0x84 + pool_shift;
  const std::size_t later_counts = counts + (major >= 9 ? 6 : 2);
  const auto count_at = [&](std::size_t index) {
    return r.u16_at(index == 0 ? counts : later_counts + (index - 1) * 2);
  };

  struct NxGroup {
    std::size_t array_field;
    std::size_t dict_field;
    std::string_view label;
    const char* magic;
    Kind kind;
  };
  const NxGroup groups_layout[] = {
      {0x28, 0x30, fmdl::Name, "FMDL", Kind::Model},
      {o + 0x00, o + 0x08, fska::Name, "FSKA", Kind::Skeletal},
      {o + 0x10, o + 0x18, fmaa::Name, "FMAA", Kind::Material},
      {o + 0x20, o + 0x28, fvis::BoneName, "FBVS", Kind::BoneVis},
      {o + 0x30, o + 0x38, fsha::Name, "FSHA", Kind::Shape},
      {o + 0x40, o + 0x48, fscn::Name, "FSCN", Kind::Scene},
  };

  const std::optional<NxLayout> layout = layout_for(major);
  if (!layout && ctx.list_members)
    out.warnings.push_back("Switch BFRES " + std::to_string(major) + ".x block layouts are unknown; contents are "
                           "listed by name only");

  std::vector<Group> groups;
  for (std::size_t i = 0; i < std::size(groups_layout); ++i) {
    const NxGroup& g = groups_layout[i];
    Group group{std::string(g.label), nx_dict(r, out, g.dict_field, ctx)};
    const u16 declared = count_at(i);
    if (group.entries.size() != declared)
      out.warnings.push_back("BFRES header counts " + std::to_string(declared) + " " + g.magic +
                             " blocks but its dictionary names " + std::to_string(group.entries.size()));
    const u64 array = r.u64_at(g.array_field);
    if (declared != 0 && (array == 0 || !r.magic_at(static_cast<std::size_t>(array), g.magic)))
      out.warnings.push_back("BFRES " + std::string(g.magic) + " array at " + fmt_hex(array) +
                             " does not start with a " + g.magic + " block");

    // Arrays hold their blocks in dictionary order.
    for (std::size_t k = 0; layout && array != 0 && k < group.entries.size(); ++k) {
      ListEntry& e = group.entries[k];
      const std::size_t at = static_cast<std::size_t>(array) + k * stride(*layout, g.kind);
      if (!r.magic_at(at, g.magic)) {
        out.warnings.push_back("BFRES " + std::string(g.magic) + " block " + std::to_string(k) + " (\"" + e.name +
                               "\") lacks its signature");
        break;
      }
      e.data = at;
      const std::string own = nx_string(r, r.u64_at(at + layout->name));
      ctx.spend(own.size(), "block names");
      if (own != e.name)
        out.warnings.push_back("BFRES " + std::string(g.magic) + " block named \"" + own +
                               "\" is listed as \"" + e.name + "\"");
      if (ctx.list_members)
        e.detail = describe(r, *layout, out, g.kind, at, e.name, ctx);
    }
    groups.push_back(std::move(group));
  }
  if (!ctx.list_members)
    return;
  add_groups(out, groups);

  // External files are a parallel array of {u64 offset, u32 size, padding}
  // entries named by their own dictionary.
  const u64 external_array = r.u64_at(o + 0x60 + pool_shift);
  const std::vector<ListEntry> external_names = nx_dict(r, out, o + 0x68 + pool_shift, ctx);
  const u16 external_count = count_at(6);
  std::vector<std::pair<u64, u64>> spans;
  if (external_array != 0) {
    r.require(static_cast<std::size_t>(external_array), std::size_t(external_count) * 0x10, "external file array");
    for (u16 i = 0; i < external_count; ++i) {
      const std::size_t e = static_cast<std::size_t>(external_array) + std::size_t(i) * 0x10;
      spans.emplace_back(r.u64_at(e), r.u32_at(e + 8));
    }
  }
  if (external_names.size() != spans.size() && !(external_names.empty() && spans.empty()))
    out.warnings.push_back("BFRES lists " + std::to_string(spans.size()) + " external files but names " +
                           std::to_string(external_names.size()));
  add_external_files(out, external_names, spans, r.data(), ctx);
}

}  // namespace nnfck::fres
