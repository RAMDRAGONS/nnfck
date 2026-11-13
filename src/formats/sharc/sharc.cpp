#include "formats/sharc/sharc.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "formats/listing.hpp"

namespace nnfck::sharc {

namespace {

using listing::count_of;

constexpr std::size_t ArrayHeaderSize = 8;
constexpr std::size_t MaxNameLength = 0x400;

// Offsets of the entries of the array at `at`, which must end exactly where
// its size says.
struct Array {
  std::size_t end;
  std::vector<std::size_t> entries;
};

Array read_array(const ByteReader& r, std::size_t at, std::size_t limit, std::size_t min_entry, std::string_view what,
                 const Context& ctx) {
  r.require(at, ArrayHeaderSize, what);
  const u32 size = r.u32_at(at);
  const u32 count = r.u32_at(at + 4);
  if (size < ArrayHeaderSize || u64{at} + size > limit)
    throw ParseError(std::string(what) + " at " + fmt_hex(at) + " claims " + fmt_hex(size) + " bytes");
  if (u64{count} * min_entry > size - ArrayHeaderSize)
    throw ParseError(std::string(what) + " claims " + std::to_string(count) + " entries in " + fmt_hex(size) +
                     " bytes");
  Array a{at + size, {}};
  ctx.spend(u64{count} * sizeof(std::size_t), what);
  a.entries.reserve(count);
  std::size_t p = at + ArrayHeaderSize;
  for (u32 i = 0; i < count; ++i) {
    const u32 entry = r.u32_at(p);
    if (entry < min_entry || u64{p} + entry > a.end)
      throw ParseError(std::string(what) + " entry " + std::to_string(i) + " at " + fmt_hex(p) + " claims " +
                       fmt_hex(entry) + " bytes");
    a.entries.push_back(p);
    p += entry;
  }
  if (p != a.end)
    throw ParseError(std::string(what) + " entries end at " + fmt_hex(p) + " instead of " + fmt_hex(a.end));
  return a;
}

// Neither archive stores a byte order mark; the version is small enough that
// only one reading of it is plausible.
ByteReader reader_for(Bytes data, std::string_view what) {
  ByteReader r(data, Endian::Little);
  r.require(0, 0x10, what);
  if (r.u32_at(0x4) > 0xFFFF)
    r.set_endian(Endian::Big);
  return r;
}

void add_common(Section& s, Analysis& out, const ByteReader& r, std::string_view signature, std::string_view what) {
  const u32 file_size = r.u32_at(0x8);
  s.add("Signature", std::string(signature));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Version", std::to_string(r.u32_at(0x4)));
  s.add("File Size", fmt_size(file_size));
  if (file_size != r.size())
    out.warnings.push_back(std::string(what) + " declares " + fmt_hex(file_size) + " bytes but holds " +
                           fmt_hex(r.size()));
}

// Shader stages, as numbered by binary entries and ordered in a program's
// per-stage tables.
constexpr std::array<const char*, 4> Stages = {"vertex", "fragment", "geometry", "compute"};

// ResShaderArchive ------------------------------------------------------------

constexpr std::size_t HeaderSize = 0x14;
constexpr std::size_t SourceHeaderSize = 0x10;
constexpr u16 NoSource = 0xFFFF;

// Programs open with their size and name length, then one source index per
// stage in the low half of a word: vertex, fragment and geometry, with
// compute added in version 13. After the name come one macro array per stage,
// then the variations.
struct Layout {
  u32 version;
  std::size_t stages;
};

constexpr Layout Layouts[] = {
    {12, 3},
    {13, 4},
};

constexpr std::size_t ProgramHeaderBase = 0x8;

}  // namespace

bool probe(Bytes data) {
  return ByteReader(data, Endian::Big).magic_at(0, "AAHS");
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(Name);
  const ByteReader r = reader_for(data, "shader archive header");
  r.require(0, HeaderSize, "shader archive header");
  Section& s = out.section(std::string(Name));
  add_common(s, out, r, "SHAA", "Shader archive");
  const u32 version = r.u32_at(0x4);
  const u32 name_length = r.u32_at(0x10);
  r.require(HeaderSize, name_length, "shader archive name");
  s.add("Name", r.cstring_at(HeaderSize, name_length));

  const Array programs = read_array(r, HeaderSize + name_length, r.size(), 8, "shader program array", ctx);
  const Array sources = read_array(r, programs.end, r.size(), SourceHeaderSize, "shader source array", ctx);
  s.add("Programs", std::to_string(programs.entries.size()));
  s.add("Sources", std::to_string(sources.entries.size()));

  std::vector<std::string> source_names;
  std::vector<std::string> source_rows;
  for (const std::size_t e : sources.entries) {
    const u32 name = r.u32_at(e + 0x4);
    const u32 text = r.u32_at(e + 0x8);
    if (u64{SourceHeaderSize} + name + text > r.u32_at(e))
      throw ParseError("shader source at " + fmt_hex(e) + " overruns its entry");
    source_names.push_back(r.cstring_at(e + SourceHeaderSize, name));
    ctx.spend(source_names.back().size(), "source names");
    source_rows.push_back(source_names.back() + ": " + fmt_size(text));
  }

  const Layout* l = nullptr;
  for (const Layout& candidate : Layouts)
    if (candidate.version == version)
      l = &candidate;
  if (l == nullptr) {
    out.warnings.push_back("Shader archive version " + std::to_string(version) +
                           " has an unknown program layout; programs are counted, not listed");
    return;
  }

  std::vector<std::string> program_rows;
  u64 variations = 0, macros = 0;
  for (const std::size_t e : programs.entries) {
    const std::size_t end = e + r.u32_at(e);
    const u32 name_size = r.u32_at(e + 0x4);
    const std::size_t header = ProgramHeaderBase + l->stages * 4;
    if (u64{header} + name_size > end - e)
      throw ParseError("shader program at " + fmt_hex(e) + " overruns its entry");
    const std::string name = r.cstring_at(e + header, name_size);
    std::size_t p = e + header + name_size;
    u32 program_macros = 0;
    for (std::size_t k = 0; k < l->stages; ++k) {
      const Array a = read_array(r, p, end, 8, "shader macro array", ctx);
      program_macros += static_cast<u32>(a.entries.size());
      p = a.end;
    }
    const Array program_variations = read_array(r, p, end, 8, "shader variation array", ctx);
    macros += program_macros;
    variations += program_variations.entries.size();
    if (!ctx.list_members)
      continue;

    std::vector<std::string> parts;
    for (std::size_t k = 0; k < l->stages; ++k) {
      const u16 index = static_cast<u16>(r.u32_at(e + ProgramHeaderBase + k * 4));
      if (index == NoSource)
        continue;
      parts.push_back(std::string(Stages[k]) + " " +
                      (index < source_names.size() ? source_names[index] : "source " + std::to_string(index)));
      if (index >= source_names.size())
        out.errors.push_back("Shader program \"" + name + "\" names source " + std::to_string(index) + " of " +
                             std::to_string(source_names.size()));
    }
    if (program_macros != 0)
      parts.push_back(count_of(program_macros, "macro"));
    if (!program_variations.entries.empty())
      parts.push_back(count_of(program_variations.entries.size(), "variation"));
    program_rows.push_back(name + (parts.empty() ? "" : ": " + listing::join(parts)));
    // Rows repeat source names, so a long name shared by many programs is
    // charged once per row.
    ctx.spend(program_rows.back().size(), "program listing");
  }
  s.add("Macros", std::to_string(macros));
  s.add("Variations", std::to_string(variations));

  if (!ctx.list_members)
    return;
  listing::Group programs_group("Programs"), sources_group("Sources");
  for (std::string& row : program_rows)
    programs_group.rows.push_back({std::move(row)});
  for (std::string& row : source_rows)
    sources_group.rows.push_back({std::move(row)});
  listing::add_groups(out, std::string(Name) + " Contents", {programs_group, sources_group});
}

// ResBinaryShaderArchive ------------------------------------------------------

namespace {

constexpr std::size_t BinaryHeaderSize = 0x18;
constexpr std::size_t NvnHeaderSize = 0x150;
constexpr std::size_t ShaderBinaryHeaderSize = 0x10;
constexpr std::size_t BinaryProgramHeaderSize = 0x10;
constexpr u32 WiiUVersion = 8;

// The string area, holding the archive name, follows the header; the shader
// binary array follows it.
bool binaries_fit(const ByteReader& r, std::size_t header) {
  const u64 at = u64{header} + r.u32_at(0x14);
  if (at + ArrayHeaderSize > r.size())
    return false;
  const u32 size = r.u32_at(static_cast<std::size_t>(at));
  return size >= ArrayHeaderSize && at + size <= r.size();
}

// NVN shader binaries carry a control section opening with this magic and a
// version word that tells the GPU generation apart.
constexpr u32 ControlMagic = 0x98761234;

struct ControlVersion {
  u32 version;
  std::string_view platform;
};

constexpr ControlVersion ControlVersions[] = {
    {1, "Switch"},
    {0xC9, "Switch 2"},
};

// The first binary's data follows its 0x10-byte entry header and points at
// its control section with a signed offset from its own start.
std::optional<u32> control_version(const ByteReader& r, std::size_t binaries) {
  if (!r.has(binaries, ArrayHeaderSize) || r.u32_at(binaries + 4) == 0)
    return std::nullopt;
  const std::size_t data = binaries + ArrayHeaderSize + ShaderBinaryHeaderSize;
  if (!r.has(data, 0x10))
    return std::nullopt;
  const std::int64_t control = static_cast<std::int64_t>(data) + r.s32_at(data + 0xC);
  if (control < 0 || !r.has(static_cast<std::size_t>(control), 8) ||
      r.u32_at(static_cast<std::size_t>(control)) != ControlMagic)
    return std::nullopt;
  return r.u32_at(static_cast<std::size_t>(control) + 4);
}

}  // namespace

bool probe_binary(Bytes data) {
  return ByteReader(data, Endian::Big).magic_at(0, "BAHS");
}

void analyze_binary(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(BinaryName);
  const ByteReader r = reader_for(data, "binary shader archive header");
  r.require(0, BinaryHeaderSize, "binary shader archive header");
  const u32 version = r.u32_at(0x4);

  // Only the NVN header leaves the space after a compact header's string
  // area empty.
  const bool nvn = !binaries_fit(r, BinaryHeaderSize) && r.has(0, NvnHeaderSize) && binaries_fit(r, NvnHeaderSize);
  const std::size_t header = nvn ? NvnHeaderSize : BinaryHeaderSize;
  const u32 strings_size = r.u32_at(0x14);
  const std::optional<u32> control = nvn ? control_version(r, header + strings_size) : std::nullopt;
  std::optional<std::string_view> platform;
  if (control) {
    for (const ControlVersion& c : ControlVersions)
      if (c.version == *control)
        platform = c.platform;
  } else if (version == WiiUVersion)
    platform = "Wii U";
  if (platform)
    out.format += " (" + std::string(*platform) + ")";

  Section& s = out.section(std::string(BinaryName));
  add_common(s, out, r, "SHAB", "Binary shader archive");
  if (platform)
    s.add("Platform", std::string(*platform));
  s.add("Extension", nvn ? ".sharcb" : ".sharcfb");

  if (control)
    s.add("Control Version", std::to_string(*control));
  const u32 name_offset = r.u32_at(0x10);
  if (name_offset >= strings_size)
    throw ParseError("archive name at " + fmt_hex(name_offset) + " lies outside its " + fmt_hex(strings_size) +
                     "-byte string area");
  s.add("Name", r.cstring_at(header + name_offset, std::min<std::size_t>(strings_size - name_offset, MaxNameLength)));

  const Array binaries = read_array(r, header + strings_size, r.size(), ShaderBinaryHeaderSize, "shader binary array", ctx);
  const Array programs = read_array(r, binaries.end, r.size(), BinaryProgramHeaderSize, "shader program array", ctx);

  std::array<u64, Stages.size() + 1> stages{};
  for (const std::size_t e : binaries.entries) {
    const u32 stage = r.u32_at(e + 0x4);
    ++stages[stage < Stages.size() ? stage : Stages.size()];
  }
  std::vector<std::string> kinds;
  for (std::size_t k = 0; k < Stages.size(); ++k)
    if (stages[k] != 0)
      kinds.push_back(std::to_string(stages[k]) + " " + Stages[k]);
  if (stages.back() != 0)
    kinds.push_back(std::to_string(stages.back()) + " of unknown stage");
  s.add("Shader Binaries",
        std::to_string(binaries.entries.size()) + (kinds.empty() ? "" : " (" + listing::join(kinds) + ")"));
  s.add("Programs", std::to_string(programs.entries.size()));

  if (nvn) {
    // Compiled code sits in one memory pool at the end of the file.
    const u32 pool_size = r.u32_at(0x18);
    const u32 pool_offset = r.u32_at(0x1C);
    s.add("Memory Pool", fmt_hex(pool_offset) + ", " + fmt_size(pool_size));
    if (u64{pool_offset} + pool_size > r.size())
      out.errors.push_back("Binary shader archive memory pool at " + fmt_hex(pool_offset) + " runs past the end of the file");
  }

  if (!ctx.list_members)
    return;
  listing::Group group("Programs");
  for (const std::size_t e : programs.entries) {
    const u32 name_size = r.u32_at(e + 0x4);
    if (u64{BinaryProgramHeaderSize} + name_size > r.u32_at(e))
      throw ParseError("shader program at " + fmt_hex(e) + " overruns its entry");
    group.rows.push_back({r.cstring_at(e + BinaryProgramHeaderSize, name_size)});
    ctx.spend(group.rows.back().text.size(), "program names");
  }
  listing::add_groups(out, std::string(BinaryName) + " Contents", {group});
}

}  // namespace nnfck::sharc
