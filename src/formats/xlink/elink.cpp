#include "core/crc32.hpp"
#include "formats/xlink/xlink.hpp"

namespace nnfck::elink {

namespace {

constexpr std::size_t HeaderSize = 0x20;
constexpr u32 KnownVersion = 32;
constexpr std::size_t UserSize = 0x14;
constexpr std::size_t RecordSize = 0xCC;
constexpr std::size_t MaxNameLength = 0x400;

constexpr std::size_t EflkHeaderSize = 0x10;
constexpr u32 EflkVersion = 9;
constexpr std::size_t EflkUserEntrySize = 0x8;
constexpr std::size_t EflkUserHeaderSize = 0x1C;
constexpr std::size_t EflkEffectSize = 0x68;
constexpr std::size_t EflkCallTableSize = 0x14;
constexpr std::size_t EflkActionTriggerSize = 0x3C;
constexpr std::size_t EflkActionSize = 0xC;

std::string count_of(u64 n, std::string_view what) {
  return std::to_string(n) + " " + std::string(what) + (n == 1 ? "" : "s");
}

// Header: file size, version, user count, record area, record count and
// string table. Each user is keyed by the CRC-32 of its name and owns a run
// of fixed-size records.
void analyze_blnk(Bytes data, Analysis& out, const Context& ctx) {
  ByteReader r(data, Endian::Big);
  r.require(0, HeaderSize, "ELink header");
  if (r.u32_at(0x8) > 0xFFFF)
    r.set_endian(Endian::Little);
  const u32 file_size = r.u32_at(0x4);
  const u32 version = r.u32_at(0x8);
  const u32 user_count = r.u32_at(0xC);
  const u32 records_at = r.u32_at(0x10);
  const u32 record_count = r.u32_at(0x14);
  const u32 strings_at = r.u32_at(0x18);

  Section& s = out.section(std::string(Name));
  s.add("Magic", fmt_quoted(data.first(4)));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Version", std::to_string(version));
  s.add("File Size", fmt_size(file_size));
  s.add("Users", std::to_string(user_count));
  s.add("Records", std::to_string(record_count));
  s.add("Record Area", fmt_hex(records_at));
  s.add("String Table", fmt_hex(strings_at));

  if (file_size != data.size())
    out.warnings.push_back("ELink database declares " + fmt_hex(file_size) + " bytes but holds " +
                           fmt_hex(data.size()));
  if (version != KnownVersion) {
    out.warnings.push_back("ELink version " + std::to_string(version) +
                           " has an unknown layout; only its header is shown");
    return;
  }
  if (u64{records_at} != HeaderSize + u64{user_count} * UserSize)
    out.errors.push_back("ELink record area at " + fmt_hex(records_at) + " does not follow the " +
                         std::to_string(user_count) + " users");
  if (u64{strings_at} != u64{records_at} + u64{record_count} * RecordSize)
    out.errors.push_back("ELink string table at " + fmt_hex(strings_at) + " does not follow the " +
                         std::to_string(record_count) + " records");
  r.require(HeaderSize, std::size_t{user_count} * UserSize, "ELink users");

  std::vector<std::string> rows;
  for (u32 i = 0; i < user_count; ++i) {
    const std::size_t e = HeaderSize + std::size_t{i} * UserSize;
    const u32 hash = r.u32_at(e);
    const u32 first = r.u32_at(e + 0x4);
    const u32 count = r.u32_at(e + 0x8);
    const std::string name = r.cstring_at(e + r.u32_at(e + 0xC), MaxNameLength);
    ctx.spend(UserSize + name.size(), "user names");
    if (crc32(name) != hash)
      out.warnings.push_back("ELink user \"" + name + "\" is stored under hash " + fmt_hex(hash, 8) +
                             " instead of " + fmt_hex(crc32(name), 8));
    if (u64{first} + count > record_count)
      out.errors.push_back("ELink user \"" + name + "\" claims records " + std::to_string(first) + " to " +
                           std::to_string(u64{first} + count) + " of " + std::to_string(record_count));
    if (ctx.list_members)
      rows.push_back(name + ": " + count_of(count, "record"));
  }

  if (rows.empty())
    return;
  Section& list = out.section(std::string(Name) + " Users");
  for (const std::string& row : rows)
    list.add("", row);
}

// Header: version, user count and string table, then a data offset and a
// name per user. Each user block repeats the version and lists its effects,
// their parameters, call tables, action triggers and actions, followed by
// its own strings.
void analyze_eflk(Bytes data, Analysis& out, const Context& ctx) {
  ByteReader r(data, Endian::Big);
  r.require(0, EflkHeaderSize, "ELink header");
  if (r.u32_at(0x4) > 0xFFFF)
    r.set_endian(Endian::Little);
  const u32 version = r.u32_at(0x4);
  const u32 user_count = r.u32_at(0x8);
  const u32 strings = r.u32_at(0xC);

  Section& s = out.section(std::string(Name));
  s.add("Magic", fmt_quoted(data.first(4)));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Version", std::to_string(version));
  s.add("Users", std::to_string(user_count));
  s.add("String Table", fmt_hex(strings));
  if (version != EflkVersion) {
    out.warnings.push_back("ELink version " + std::to_string(version) +
                           " has an unknown layout; only its header is shown");
    return;
  }
  r.require(EflkHeaderSize, std::size_t{user_count} * EflkUserEntrySize, "ELink users");

  // Call tables, effects, actions, action triggers.
  u64 totals[4] = {};
  std::vector<std::string> rows;
  for (u32 i = 0; i < user_count; ++i) {
    const std::size_t e = EflkHeaderSize + std::size_t{i} * EflkUserEntrySize;
    const std::size_t at = r.u32_at(e);
    const std::string name = r.cstring_at(std::size_t{strings} + r.u32_at(e + 4), MaxNameLength);
    ctx.spend(EflkUserEntrySize + name.size(), "user names");
    r.require(at, EflkUserHeaderSize, "ELink user");
    const u32 user_version = r.u32_at(at);
    const u32 effects = r.u32_at(at + 0x4);
    const u32 triggers = r.u32_at(at + 0x8);
    const u32 call_tables = r.u32_at(at + 0xC);
    const u32 actions = r.u32_at(at + 0x10);
    const u32 pool = r.u32_at(at + 0x14);
    const u32 params_per_effect = r.u32_at(at + 0x18);
    const u64 tables = EflkUserHeaderSize + u64{effects} * EflkEffectSize + u64{effects} * params_per_effect * 4 +
                       u64{call_tables} * EflkCallTableSize + u64{triggers} * EflkActionTriggerSize +
                       u64{actions} * EflkActionSize;
    if (user_version != EflkVersion)
      out.errors.push_back("ELink user \"" + name + "\" carries version " + std::to_string(user_version));
    else if (pool != tables)
      out.errors.push_back("ELink user \"" + name + "\" places its strings at " + fmt_hex(pool) +
                           " but its tables end at " + fmt_hex(tables));
    else if (!r.has(at, pool))
      out.errors.push_back("ELink user \"" + name + "\" runs past the end of the file");
    totals[0] += call_tables;
    totals[1] += effects;
    totals[2] += actions;
    totals[3] += triggers;
    if (ctx.list_members)
      rows.push_back(name + ": " + count_of(call_tables, "call table") + ", " + count_of(effects, "effect") + ", " +
                     count_of(actions, "action") + ", " + count_of(triggers, "action trigger"));
  }
  s.add("Call Tables", std::to_string(totals[0]));
  s.add("Effects", std::to_string(totals[1]));
  s.add("Actions", std::to_string(totals[2]));
  s.add("Action Triggers", std::to_string(totals[3]));

  if (rows.empty())
    return;
  Section& list = out.section(std::string(Name) + " Users");
  for (const std::string& row : rows)
    list.add("", row);
}

}  // namespace

bool probe(Bytes data) {
  const ByteReader r(data, Endian::Big);
  return r.magic_at(0, "BLNK") || r.magic_at(0, "eflk");
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(Name);
  if (ByteReader(data, Endian::Big).magic_at(0, "eflk"))
    analyze_eflk(data, out, ctx);
  else
    analyze_blnk(data, out, ctx);
}

}  // namespace nnfck::elink
