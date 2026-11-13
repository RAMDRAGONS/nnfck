#include "formats/fsha/fsha.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "formats/bnsh/bnsh.hpp"
#include "formats/byte_order.hpp"
#include "formats/listing.hpp"

namespace nnfck::fsha {

namespace {

using listing::count_of;

constexpr std::size_t DictNodeSize = 0x10;
constexpr std::size_t MaxNameLength = 0x400;

struct Flag {
  u32 bit;
  const char* name;
};

std::string flag_names(u32 flags, const std::vector<Flag>& table) {
  std::vector<std::string> names;
  for (const Flag& f : table)
    if (flags & f.bit)
      names.push_back(f.name);
  return fmt_hex(flags, 4) + (names.empty() ? "" : " (" + listing::join(names) + ")");
}

struct Model {
  std::string name;
  u32 programs = 0, static_options = 0, dynamic_options = 0, attributes = 0, samplers = 0, uniform_blocks = 0;
  std::optional<u32> uniforms;
  std::optional<bnsh::Summary> shader;
  std::string shader_error;
};

std::string describe(const Model& m) {
  std::vector<std::string> parts{count_of(m.programs, "program"),
                                 count_of(m.static_options, "static option"),
                                 count_of(m.dynamic_options, "dynamic option"),
                                 count_of(m.attributes, "attribute"),
                                 count_of(m.samplers, "sampler"),
                                 count_of(m.uniform_blocks, "uniform block")};
  if (m.uniforms)
    parts.push_back(count_of(*m.uniforms, "uniform"));
  if (m.shader)
    parts.push_back(bnsh::describe(*m.shader));
  return m.name + ": " + listing::join(parts);
}

void add_models(Section& s, Analysis& out, const std::vector<Model>& models, const Context& ctx) {
  u64 programs = 0, variations = 0;
  for (const Model& m : models) {
    programs += m.programs;
    if (m.shader)
      variations += m.shader->variations;
  }
  s.add("Shading Models", std::to_string(models.size()));
  s.add("Shader Programs", std::to_string(programs));
  if (variations != 0)
    s.add("Shader Variations", std::to_string(variations));
  if (!ctx.list_members || models.empty())
    return;
  Section& list = out.section(std::string(Name) + " Shading Models");
  for (const Model& m : models) {
    list.add("", describe(m));
    ctx.spend(list.fields().back().value.size(), "shading model listing");
  }
}

// Switch (nn::g3d) ------------------------------------------------------------

const std::vector<Flag> NxFlags = {{0x01, "Source"},         {0x02, "Binary"},          {0x08, "Binary Available"},
                                   {0x10, "Force Variation"}, {0x20, "Offline DL"},      {0x40, "IR"}};

// Shading models are a fixed-size array of pointer slots followed by counts.
// Later versions add shader storage blocks and then images, moving the slots
// after them and the counts.
struct NxLayout {
  u16 major;
  std::size_t archive_count;  // u16 model count, then u16 flags
  std::size_t model_size;
  std::size_t static_dic, dynamic_dic, attr_dic, sampler_dic, uniform_block_dic, program_array, shader_file;
  std::size_t uniforms, static_count, dynamic_count, program_count;
  std::size_t program_size;
};

constexpr NxLayout NxLayouts[] = {
    {3, 0x38, 0xC0, 2, 4, 6, 8, 10, 12, 16, 0xA0, 0xA8, 0xAA, 0xAC, 0x30},
    {7, 0x40, 0xE8, 2, 4, 6, 8, 10, 15, 19, 0xC8, 0xD4, 0xD6, 0xD8, 0x38},
    {9, 0x40, 0x100, 2, 4, 6, 8, 12, 17, 21, 0xD8, 0xE4, 0xE6, 0xE8, 0x40},
};

// Strings are a u16 length followed by the characters.
std::string nx_string(const ByteReader& r, u64 at) {
  if (at == 0)
    return {};
  const u16 length = r.u16_at(static_cast<std::size_t>(at));
  r.require(static_cast<std::size_t>(at) + 2, length, "string");
  const Bytes s = r.data().subspan(static_cast<std::size_t>(at) + 2, length);
  return std::string(s.begin(), s.end());
}

// nn::util::ResDic: "_DIC" or zeroes, an entry count, then a root node and
// one 16-byte node per entry.
u32 nx_dict_count(const ByteReader& r, u64 dict) {
  if (dict == 0)
    return 0;
  const s32 count = r.s32_at(static_cast<std::size_t>(dict) + 4);
  if (count < 0 || u64{static_cast<u32>(count) + 1} * DictNodeSize > r.size() - dict - 8)
    throw ParseError("dictionary at " + fmt_hex(dict) + " claims " + std::to_string(count) + " entries");
  return static_cast<u32>(count);
}

void analyze_nx(Bytes data, Analysis& out, const Context& ctx) {
  ByteReader r(data, Endian::Little);
  r.require(0, 0x38, "BFSHA header");
  r.set_endian(read_bom(r, 0xC));
  const u32 version = r.u32_at(0x8);
  const u16 major = static_cast<u16>(version >> 16);
  const u32 file_size = r.u32_at(0x1C);
  const u64 archive = r.u64_at(0x20);

  Section& s = out.section(std::string(Name));
  s.add("Version", std::to_string(major) + "." + std::to_string((version >> 8) & 0xFF) + "." +
                       std::to_string(version & 0xFF));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("File Size", fmt_size(file_size));
  if (file_size != data.size())
    out.warnings.push_back("BFSHA declares " + fmt_hex(file_size) + " bytes but holds " + fmt_hex(data.size()));
  if (const u32 reloc = r.u32_at(0x18); reloc != 0 && !r.magic_at(reloc, "_RLT"))
    out.warnings.push_back("BFSHA relocation table at " + fmt_hex(reloc) + " lacks its _RLT signature");

  r.require(static_cast<std::size_t>(archive), 0x20, "BFSHA shader archive");
  s.add("Name", nx_string(r, r.u64_at(static_cast<std::size_t>(archive))));
  if (const std::string path = nx_string(r, r.u64_at(static_cast<std::size_t>(archive) + 8)); !path.empty())
    s.add("Path", path);
  const u64 models_at = r.u64_at(static_cast<std::size_t>(archive) + 0x10);
  const u32 named = nx_dict_count(r, r.u64_at(static_cast<std::size_t>(archive) + 0x18));

  const NxLayout* l = nullptr;
  for (const NxLayout& candidate : NxLayouts)
    if (candidate.major == major)
      l = &candidate;
  if (l == nullptr) {
    s.add("Shading Models", std::to_string(named));
    out.warnings.push_back("BFSHA version " + std::to_string(major) +
                           ".x has no known shading model layout; models are counted, not listed");
    return;
  }
  const u16 count = r.u16_at(static_cast<std::size_t>(archive) + l->archive_count);
  s.add("Flags", flag_names(r.u16_at(static_cast<std::size_t>(archive) + l->archive_count + 2), NxFlags));
  if (count != named)
    out.warnings.push_back("BFSHA archive counts " + std::to_string(count) + " shading models but its dictionary names " +
                           std::to_string(named));
  r.require(static_cast<std::size_t>(models_at), std::size_t{count} * l->model_size, "BFSHA shading models");

  std::vector<Model> models;
  std::optional<std::string_view> platform;
  for (u16 i = 0; i < count; ++i) {
    const std::size_t m = static_cast<std::size_t>(models_at) + std::size_t{i} * l->model_size;
    const auto slot = [&](std::size_t index) { return r.u64_at(m + index * 8); };
    Model model;
    model.name = nx_string(r, slot(0));
    ctx.spend(l->model_size + model.name.size(), "shading models");
    model.programs = r.u16_at(m + l->program_count);
    model.static_options = r.u16_at(m + l->static_count);
    model.dynamic_options = r.u16_at(m + l->dynamic_count);
    model.uniforms = r.u32_at(m + l->uniforms);
    model.attributes = nx_dict_count(r, slot(l->attr_dic));
    model.samplers = nx_dict_count(r, slot(l->sampler_dic));
    model.uniform_blocks = nx_dict_count(r, slot(l->uniform_block_dic));
    if (nx_dict_count(r, slot(l->static_dic)) != model.static_options ||
        nx_dict_count(r, slot(l->dynamic_dic)) != model.dynamic_options)
      out.warnings.push_back("BFSHA shading model \"" + model.name + "\" counts options its dictionaries do not name");
    if (model.programs != 0)
      r.require(static_cast<std::size_t>(slot(l->program_array)), std::size_t{model.programs} * l->program_size,
                "BFSHA shader programs");

    if (const u64 file = slot(l->shader_file); file != 0) {
      try {
        if (file >= data.size())
          throw ParseError("lies outside the file");
        model.shader = bnsh::summarize(data.subspan(static_cast<std::size_t>(file)), ctx);
        if (!platform)
          platform = model.shader->platform;
      } catch (const ParseError& e) {
        out.errors.push_back("BFSHA shading model \"" + model.name + "\" shader file at " + fmt_hex(file) + ": " +
                             e.what());
      }
    }
    models.push_back(std::move(model));
  }

  if (platform) {
    out.format += " (" + std::string(*platform) + ")";
    s.add("Platform", std::string(*platform));
  }
  add_models(s, out, models, ctx);
}

// Wii U (nw::g3d) -------------------------------------------------------------

const std::vector<Flag> CafeFlags = {{0x01, "GL Source"}, {0x02, "GL Binary"}, {0x04, "Compressed"},
                                     {0x08, "GL Binary Available"}, {0x10, "Force Variation"},
                                     {0x20, "Offline DL"}};

constexpr std::size_t CafeHeaderSize = 0x30;

// Where a shading model keeps its uniform count and its own name. Later
// versions add a fourth shader stage to the per-stage limits ahead of them.
struct CafeModelLayout {
  std::size_t uniforms;
  std::size_t name;
};

constexpr CafeModelLayout CafeModelLayouts[] = {
    {0x14, 0x20},
    {0x18, 0x24},
};

// Offsets are relative to the field holding them; zero means none.
std::optional<std::size_t> cafe_offset(const ByteReader& r, std::size_t field) {
  const s32 v = r.s32_at(field);
  if (v == 0)
    return std::nullopt;
  const std::int64_t at = static_cast<std::int64_t>(field) + v;
  if (at < 0 || static_cast<u64>(at) >= r.size())
    throw ParseError("offset at " + fmt_hex(field) + " points outside the file");
  return static_cast<std::size_t>(at);
}

std::string cafe_string(const ByteReader& r, std::size_t field) {
  const std::optional<std::size_t> at = cafe_offset(r, field);
  return at ? r.cstring_at(*at, MaxNameLength) : std::string();
}

// The shading model dictionary: its byte size, entry count, a root node and
// one 16-byte node per entry. Earlier archives keep its offset where later
// ones store an ABI version, so the field that leads to a well-formed
// dictionary is the one in use.
std::optional<std::size_t> cafe_model_dict(const ByteReader& r, u16 count) {
  for (const std::size_t field : {std::size_t{0x28}, std::size_t{0x2C}}) {
    const s32 v = r.s32_at(field);
    const std::int64_t at = static_cast<std::int64_t>(field) + v;
    if (v == 0 || at < 0 || !r.has(static_cast<std::size_t>(at), 8))
      continue;
    const std::size_t d = static_cast<std::size_t>(at);
    if (r.u32_at(d) == 8 + (u32{count} + 1) * DictNodeSize && r.s32_at(d + 4) == count)
      return d;
  }
  return std::nullopt;
}

void analyze_cafe(Bytes data, Analysis& out, const Context& ctx) {
  ByteReader r(data, Endian::Big);
  r.require(0, CafeHeaderSize, "BFSHA header");
  r.set_endian(read_bom(r, 0x8));
  out.format += " (Wii U)";
  const u32 file_size = r.u32_at(0xC);

  Section& s = out.section(std::string(Name));
  s.add("Platform", "Wii U");
  s.add("Version", std::to_string(r.u8_at(4)) + "." + std::to_string(r.u8_at(5)) + "." + std::to_string(r.u8_at(6)) +
                       "." + std::to_string(r.u8_at(7)));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Alignment", fmt_hex(r.u32_at(0x10)));
  s.add("File Size", fmt_size(file_size));
  if (file_size != data.size())
    out.warnings.push_back("BFSHA declares " + fmt_hex(file_size) + " bytes but holds " + fmt_hex(data.size()));
  s.add("Name", cafe_string(r, 0x14));
  if (const std::string path = cafe_string(r, 0x20); !path.empty())
    s.add("Path", path);
  const u16 count = r.u16_at(0x24);
  s.add("Flags", flag_names(r.u16_at(0x26), CafeFlags));

  const std::optional<std::size_t> dict = cafe_model_dict(r, count);
  if (!dict) {
    s.add("Shading Models", std::to_string(count));
    out.warnings.push_back("BFSHA shading model dictionary not found; models are counted, not listed");
    return;
  }
  r.require(*dict, 8 + (std::size_t{count} + 1) * DictNodeSize, "BFSHA shading model dictionary");

  // Each shading model opens with its key lengths and counts. The layout in
  // use is the one whose name field leads back to the model's own name.
  std::vector<Model> models;
  for (u16 i = 1; i <= count; ++i) {
    const std::size_t node = *dict + 8 + std::size_t{i} * DictNodeSize;
    Model model;
    model.name = cafe_string(r, node + 0x8);
    ctx.spend(DictNodeSize + model.name.size(), "shading models");
    const std::optional<std::size_t> m = cafe_offset(r, node + 0xC);
    if (!m)
      throw ParseError("shading model \"" + model.name + "\" has no data");
    r.require(*m, 0x28, "BFSHA shading model");
    model.static_options = r.u16_at(*m + 0x2);
    model.dynamic_options = r.u16_at(*m + 0x4);
    model.programs = r.u16_at(*m + 0x6);
    model.attributes = r.u8_at(*m + 0x8);
    model.samplers = r.u8_at(*m + 0x9);
    model.uniform_blocks = r.u8_at(*m + 0xA);
    const std::int64_t name_at = static_cast<std::int64_t>(node + 0x8) + r.s32_at(node + 0x8);
    for (const CafeModelLayout& l : CafeModelLayouts) {
      if (static_cast<std::int64_t>(*m + l.name) + r.s32_at(*m + l.name) == name_at) {
        model.uniforms = static_cast<u32>(r.s32_at(*m + l.uniforms));
        break;
      }
    }
    if (!model.uniforms)
      out.warnings.push_back("BFSHA shading model \"" + model.name + "\" has an unknown layout; uniforms are not counted");
    models.push_back(std::move(model));
  }
  add_models(s, out, models, ctx);
}

}  // namespace

bool probe(Bytes data) {
  return ByteReader(data, Endian::Big).magic_at(0, "FSHA");
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(Name);
  if (ByteReader(data, Endian::Big).magic_at(0, "FSHA    "))
    analyze_nx(data, out, ctx);
  else
    analyze_cafe(data, out, ctx);
}

}  // namespace nnfck::fsha
