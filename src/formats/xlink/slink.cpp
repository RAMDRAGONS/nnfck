#include "formats/listing.hpp"
#include "formats/xlink/pooled.hpp"
#include "formats/xlink/xlink.hpp"

namespace nnfck::slink {

namespace {

using listing::count_of;

constexpr std::size_t HeaderSize = 0x10;
// A u32 follows each user's tables; the user header points at it.
constexpr std::size_t UserTrailerSize = 4;
constexpr std::size_t MaxNameLength = 0x400;

// Per user: call tables (named sound calls, each owning a run of assets),
// actions (each owning a run of action triggers), assets and action
// triggers, in that order after the user header.
struct Layout {
  u32 version;
  std::size_t user_header;
  std::size_t call_table;
  std::size_t action;
  std::size_t asset;
  std::size_t action_trigger;
};

constexpr Layout Layouts[] = {
    {50, 0x3C, 0xC, 0xC, 0x44, 0x3C},
    {69, 0x30, 0x14, 0x14, 0x5C, 0x4C},
};

// Assets and action triggers move into pools shared by all users.
constexpr u32 PooledVersion = 73;
constexpr pooled_link::Layout PooledLayout = {
    .name = Name,
    .library = "SoundLink",
    .asset = "asset",
    .assets_title = "Assets",
    .header = 0x18,
    .users_field = 0xC,
    .assets_field = 0x10,
    .triggers_field = 0x14,
    .asset_size = 0x4C,
    .trigger_size = 0x40,
    .user_header = 0x18,
    .call_tables_field = 0x10,
    .actions_field = 0x12,
    .user_assets_field = 0x14,
    .user_triggers_field = 0x16,
    .params_field = 0,
    .call_table_size = 0x14,
    .action_size = 0x14,
    .trailer_offset = true,
};

const Layout* layout_for(u32 version) {
  for (const Layout& l : Layouts)
    if (l.version == version)
      return &l;
  return nullptr;
}

}  // namespace

bool probe(Bytes data) {
  return ByteReader(data, Endian::Big).magic_at(0, "SLNK");
}

// Header: file size, version, user count, then one file offset per user. The
// string table starts right after the offsets; user blocks name their
// strings relative to it.
void analyze(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(Name);
  ByteReader r(data, Endian::Big);
  r.require(0, HeaderSize, "SoundLink header");
  if (r.u32_at(0x8) > 0xFFFF)
    r.set_endian(Endian::Little);
  const u32 file_size = r.u32_at(0x4);
  const u32 version = r.u32_at(0x8);
  const u32 user_count = r.u32_at(0xC);
  const u64 strings = HeaderSize + u64{user_count} * 4;

  Section& s = out.section(std::string(Name));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Version", std::to_string(version));
  s.add("File Size", fmt_size(file_size));
  if (file_size != data.size())
    out.warnings.push_back("SoundLink database declares " + fmt_hex(file_size) + " bytes but holds " +
                           fmt_hex(data.size()));
  if (version == PooledVersion) {
    s.add("Platform", "3DS");
    out.format += " (3DS)";
    pooled_link::analyze(r, s, out, ctx, PooledLayout);
    return;
  }
  s.add("Users", std::to_string(user_count));
  s.add("String Table", fmt_hex(strings));
  const Layout* l = layout_for(version);
  if (l == nullptr) {
    out.warnings.push_back("SoundLink version " + std::to_string(version) +
                           " has an unknown layout; only its header is shown");
    return;
  }
  r.require(HeaderSize, std::size_t{user_count} * 4, "SoundLink user offsets");

  // Call tables, actions, assets, action triggers.
  u64 totals[4] = {};
  std::vector<std::string> rows;
  for (u32 i = 0; i < user_count; ++i) {
    const std::size_t at = r.u32_at(HeaderSize + std::size_t{i} * 4);
    r.require(at, l->user_header, "SoundLink user");
    const u32 block_size = r.u32_at(at + 0x4);
    const std::string name = r.cstring_at(static_cast<std::size_t>(strings) + r.u32_at(at + 0x8), MaxNameLength);
    ctx.spend(l->user_header + name.size(), "user names");
    const u32 counts[4] = {r.u32_at(at + 0xC), r.u32_at(at + 0x10), r.u32_at(at + 0x14), r.u32_at(at + 0x18)};
    const u64 tables = l->user_header + u64{counts[0]} * l->call_table + u64{counts[1]} * l->action +
                       u64{counts[2]} * l->asset + u64{counts[3]} * l->action_trigger;
    if (block_size != tables + UserTrailerSize || r.u32_at(at + 0x1C) != tables)
      out.errors.push_back("SoundLink user \"" + name + "\" spans " + fmt_hex(block_size) +
                           " bytes but its tables need " + fmt_hex(tables + UserTrailerSize));
    else if (!r.has(at, block_size))
      out.errors.push_back("SoundLink user \"" + name + "\" runs past the end of the file");
    for (int k = 0; k < 4; ++k)
      totals[k] += counts[k];
    if (ctx.list_members)
      rows.push_back(name + ": " + count_of(counts[0], "call table") + ", " + count_of(counts[2], "asset") + ", " +
                     count_of(counts[1], "action") + ", " + count_of(counts[3], "action trigger"));
  }
  s.add("Call Tables", std::to_string(totals[0]));
  s.add("Assets", std::to_string(totals[2]));
  s.add("Actions", std::to_string(totals[1]));
  s.add("Action Triggers", std::to_string(totals[3]));

  if (rows.empty())
    return;
  Section& list = out.section(std::string(Name) + " Users");
  for (const std::string& row : rows)
    list.add("", row);
}

}  // namespace nnfck::slink
