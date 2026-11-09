#include "formats/xlink/xlink.hpp"

#include <array>
#include <vector>

namespace nnfck::xlink {

namespace {

constexpr std::size_t VersionField = 0x8;
constexpr u32 FirstSharedLayoutVersion = 27;
constexpr u32 LastKnownVersion = 36;
constexpr u32 LegacyVersion = 19;
constexpr u32 NamedUserVersion = 21;
constexpr std::size_t NamedUserHeaderSize = 0x4C;
constexpr std::size_t NamedUserEntrySize = 0x8;
constexpr std::size_t HeaderSize = 0x48;
constexpr std::size_t LegacyHeaderSize = 0x40;
constexpr std::size_t ParamTableHeaderSize = 0x14;
constexpr std::size_t ParamDefineSize = 0xC;
constexpr std::size_t WideHeaderSize = 0x60;
constexpr std::size_t WideParamDefineSize = 0x18;
// Narrow files keep their name table position here; wide files keep the
// upper half of a 64-bit position, which no file is large enough to set.
constexpr std::size_t WidthProbeField = 0x44;
constexpr std::size_t LegacyParamDefineSize = 0x8;
constexpr std::size_t MaxNameLength = 0x400;

const std::array<const char*, 6> ParamTypes = {"Int", "Float", "Bool", "Enum", "String", "Bitfield"};

std::string param_type(u32 type) {
  return type < ParamTypes.size() ? ParamTypes[type] : "Unknown type " + std::to_string(type);
}

struct ParamDefine {
  std::string name;
  u32 type;
};

// User, asset and trigger parameter definitions; the last `custom_asset`
// asset parameters are defined by the game rather than the library.
struct ParamTable {
  u32 user = 0, asset = 0, custom_asset = 0, trigger = 0;
  std::vector<ParamDefine> defines;
};

// The standard asset parameters name what the system plays: emitter sets for
// effects, volume for sound.
std::string_view system_of(const ParamTable& t) {
  const std::size_t standard_end = t.user + (t.asset - t.custom_asset);
  for (std::size_t i = t.user; i < standard_end && i < t.defines.size(); ++i) {
    if (t.defines[i].name == "Eset")
      return "ELink2";
    if (t.defines[i].name == "Volume")
      return "SLink2";
  }
  return {};
}

// Each definition opens with its name's offset into the string table, as wide
// as the file's positions, followed by its type.
void read_defines(const ByteReader& r, ParamTable& t, std::size_t at, std::size_t stride, std::size_t name_width,
                  std::size_t strings, const Context& ctx) {
  const u64 total = u64{t.user} + t.asset + t.trigger;
  if (t.custom_asset > t.asset)
    throw ParseError("parameter table claims " + std::to_string(t.custom_asset) + " custom asset parameters of " +
                     std::to_string(t.asset));
  r.require(at, static_cast<std::size_t>(total * stride), "parameter definitions");
  for (u64 i = 0; i < total; ++i) {
    const std::size_t e = at + static_cast<std::size_t>(i) * stride;
    const u64 name = name_width == 8 ? r.u64_at(e) : r.u32_at(e);
    if (name >= r.size())
      throw ParseError("parameter definition " + std::to_string(i) + " names string " + fmt_hex(name) +
                       ", outside the file");
    ParamDefine d{r.cstring_at(strings + static_cast<std::size_t>(name), MaxNameLength), r.u32_at(e + name_width)};
    ctx.spend(stride + d.name.size(), "parameter names");
    t.defines.push_back(std::move(d));
  }
}

void add_param_rows(Section& s, const ParamTable& t) {
  s.add("Parameter Definitions", "");
  s.add("User", std::to_string(t.user), 1);
  s.add("Asset", std::to_string(t.asset) + " (" + std::to_string(t.custom_asset) + " custom)", 1);
  s.add("Trigger", std::to_string(t.trigger), 1);
}

void add_param_list(Analysis& out, const ParamTable& t) {
  Section& s = out.section(std::string(Name) + " Parameters");
  const auto group = [&](const char* label, std::size_t begin, std::size_t count, std::size_t custom_from) {
    if (count == 0)
      return;
    s.add(label, std::to_string(count));
    for (std::size_t i = begin; i < begin + count; ++i)
      s.add("", t.defines[i].name + ": " + param_type(t.defines[i].type) + (i >= custom_from ? " (custom)" : ""), 1);
  };
  const std::size_t asset_begin = t.user, trigger_begin = t.user + t.asset;
  group("User", 0, t.user, t.user);
  group("Asset", asset_begin, t.asset, trigger_begin - t.custom_asset);
  group("Trigger", trigger_begin, t.trigger, trigger_begin + t.trigger);
}

}  // namespace

bool probe(Bytes data) {
  return ByteReader(data, Endian::Big).magic_at(0, "XLNK");
}

// Neither byte order nor a byte order mark is stored; the version is small
// enough that only one reading of it is plausible.
void analyze(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(Name);
  ByteReader r(data, Endian::Big);
  r.require(0, LegacyHeaderSize, "XLink header");
  if (r.u32_at(VersionField) > 0xFFFF)
    r.set_endian(Endian::Little);
  const u32 data_size = r.u32_at(0x4);
  const u32 version = r.u32_at(VersionField);

  Section& s = out.section(std::string(Name));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Resource Version", std::to_string(version));
  s.add("Data Size", fmt_size(data_size));
  if (data_size != data.size())
    out.warnings.push_back("XLink database declares " + fmt_hex(data_size) + " bytes but holds " +
                           fmt_hex(data.size()));

  ParamTable t;
  u32 users = 0;
  std::vector<std::string> user_names;
  std::vector<std::pair<std::string, u32>> counts;
  if (version == LegacyVersion) {
    // The parameter table counts sit in the header; definitions follow the
    // user offsets and name the strings in the name table.
    t.user = r.u32_at(0x0C);
    t.asset = r.u32_at(0x10);
    t.custom_asset = r.u32_at(0x14);
    t.trigger = r.u32_at(0x18);
    users = r.u32_at(0x30);
    const std::size_t defines = LegacyHeaderSize + std::size_t{users} * 4;
    read_defines(r, t, defines, LegacyParamDefineSize, 4, r.u32_at(0x3C), ctx);
  } else if (version == NamedUserVersion) {
    // Same inline parameter table counts, but users pair a data offset with a
    // name, and the name table offset moves to the end of a longer header.
    r.require(0, NamedUserHeaderSize, "XLink header");
    t.user = r.u32_at(0x0C);
    t.asset = r.u32_at(0x10);
    t.custom_asset = r.u32_at(0x14);
    t.trigger = r.u32_at(0x18);
    users = r.u32_at(0x40);
    const std::size_t names = r.u32_at(0x48);
    counts = {{"Resource Parameters", r.u32_at(0x1C)},
              {"Trigger Overwrite Parameters", r.u32_at(0x20)},
              {"Local Property Names", r.u32_at(0x24)},
              {"Local Property Enum Names", r.u32_at(0x28)},
              {"Direct Values", r.u32_at(0x2C)},
              {"Random Tables", r.u32_at(0x30)},
              {"Curves", r.u32_at(0x34)},
              {"Curve Points", r.u32_at(0x38)}};
    r.require(NamedUserHeaderSize, std::size_t{users} * NamedUserEntrySize, "XLink users");
    for (u32 i = 0; i < users; ++i) {
      const std::size_t e = NamedUserHeaderSize + std::size_t{i} * NamedUserEntrySize;
      std::string name = r.cstring_at(names + r.u32_at(e + 4), MaxNameLength);
      ctx.spend(NamedUserEntrySize + name.size(), "user names");
      user_names.push_back(std::move(name));
    }
    const std::size_t defines = NamedUserHeaderSize + std::size_t{users} * NamedUserEntrySize;
    read_defines(r, t, defines, LegacyParamDefineSize, 4, names, ctx);
  } else if (version >= FirstSharedLayoutVersion && version <= LastKnownVersion) {
    // Later files widen every table position to 64 bits; the field order is
    // otherwise unchanged.
    r.require(0, HeaderSize, "XLink header");
    const bool wide = r.has(0, WideHeaderSize) && r.u32_at(WidthProbeField) == 0;
    s.add("Position Width", wide ? "64-bit" : "32-bit");
    const std::size_t shift = wide ? 8 : 0;
    users = r.u32_at(wide ? 0x48 : 0x3C);
    counts = {{"Resource Parameters", r.u32_at(0x0C)},
              {"Resource Asset Parameters", r.u32_at(0x10)},
              {"Trigger Overwrite Parameters", r.u32_at(0x14)},
              {"Local Property Names", r.u32_at(0x20 + shift)},
              {"Local Property Enum Names", r.u32_at(0x24 + shift)},
              {"Direct Values", r.u32_at(0x28 + shift)},
              {"Random Tables", r.u32_at(0x2C + shift)},
              {"Curves", r.u32_at(0x30 + shift)},
              {"Curve Points", r.u32_at(0x34 + shift)}};
    // A name hash per user, then an offset per user, precede the parameter
    // table; wide offsets and the table's definitions are 8-byte aligned.
    const std::size_t align = wide ? 8 : 1;
    const auto align_up = [align](std::size_t v) { return (v + align - 1) / align * align; };
    const std::size_t table =
        align_up((wide ? WideHeaderSize : HeaderSize) + std::size_t{users} * 4) + std::size_t{users} * (wide ? 8 : 4);
    r.require(table, ParamTableHeaderSize, "parameter table header");
    const u32 table_size = r.u32_at(table);
    t.user = r.u32_at(table + 0x4);
    t.asset = r.u32_at(table + 0x8);
    t.custom_asset = r.u32_at(table + 0xC);
    t.trigger = r.u32_at(table + 0x10);
    const std::size_t stride = wide ? WideParamDefineSize : ParamDefineSize;
    const std::size_t defines = align_up(table + ParamTableHeaderSize);
    const std::size_t strings = defines + (std::size_t{t.user} + t.asset + t.trigger) * stride;
    if (strings > table + std::size_t{table_size})
      throw ParseError("parameter definitions overrun their " + fmt_hex(table_size) + "-byte table");
    read_defines(r, t, defines, stride, wide ? 8 : 4, strings, ctx);
  } else {
    out.warnings.push_back("XLink resource version " + std::to_string(version) +
                           " has an unknown layout; only its header is shown");
    return;
  }

  const std::string_view system = system_of(t);
  s.add("System", system.empty() ? "Unknown" : std::string(system) + (system == "ELink2" ? " (effects)" : " (sound)"));
  if (!system.empty())
    out.format += " (" + std::string(system) + ")";
  s.add("Users", std::to_string(users));
  add_param_rows(s, t);
  for (const auto& [label, n] : counts)
    s.add(label, std::to_string(n));
  if (!ctx.list_members)
    return;
  add_param_list(out, t);
  if (user_names.empty())
    return;
  Section& list = out.section(std::string(Name) + " Users");
  for (const std::string& name : user_names)
    list.add("", name);
}

}  // namespace nnfck::xlink
