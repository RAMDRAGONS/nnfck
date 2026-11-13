#include "formats/xlink/pooled.hpp"

#include <set>
#include <string>
#include <vector>

#include "core/shift_jis.hpp"
#include "formats/listing.hpp"

namespace nnfck::pooled_link {

namespace {

using listing::count_of;

constexpr u32 AliasUser = 2;
constexpr u32 EmptyUser = 3;
constexpr std::size_t AliasUserSize = 0x10;
// Stands in for a user's own copies of its assets or action triggers,
// sending lookups through its index list into the shared pool instead.
constexpr u32 SharedPoolMarker = 0xFFFFFFFF;
constexpr std::size_t MaxNameLength = 0x400;

struct User {
  std::string name;
  std::string base;
  bool alias;
};

}  // namespace

void analyze(const ByteReader& r, Section& s, Analysis& out, const Context& ctx, const Layout& l) {
  r.require(0, l.header, std::string(l.library) + " header");
  const u32 user_count = r.u32_at(l.users_field);
  const u32 shared_assets = r.u32_at(l.assets_field);
  const u32 shared_triggers = r.u32_at(l.triggers_field);
  const u64 offsets = l.header + u64{shared_assets} * l.asset_size + u64{shared_triggers} * l.trigger_size;
  const u64 strings = offsets + u64{user_count} * 4;
  s.add("Shared " + std::string(l.assets_title), std::to_string(shared_assets));
  s.add("Shared Action Triggers", std::to_string(shared_triggers));
  s.add("String Table", fmt_hex(strings));
  r.require(static_cast<std::size_t>(offsets), std::size_t{user_count} * 4, std::string(l.library) + " user offsets");

  const std::string lib(l.library);
  std::vector<User> users;
  // Call tables, assets, actions, action triggers.
  u64 totals[4] = {};
  std::vector<std::string> rows;
  for (u32 i = 0; i < user_count; ++i) {
    const std::size_t at = r.u32_at(static_cast<std::size_t>(offsets) + std::size_t{i} * 4);
    r.require(at, AliasUserSize, lib + " user");
    const u32 kind = r.u32_at(at);
    if (kind == EmptyUser)
      continue;
    // Names come from Japanese Windows tools.
    const auto name_at = [&](std::size_t field) {
      return shift_jis_to_utf8(r.cstring_at(static_cast<std::size_t>(strings) + r.u32_at(at + field), MaxNameLength));
    };
    User u{name_at(0x4), name_at(0x8), kind == AliasUser};
    ctx.spend(AliasUserSize + u.name.size() + u.base.size(), "user names");
    if (u.alias) {
      if (ctx.list_members)
        rows.push_back(u.name + ": alias of " + u.base);
      users.push_back(std::move(u));
      continue;
    }

    r.require(at, l.user_header, lib + " user");
    const u32 call_tables = r.u16_at(at + l.call_tables_field);
    const u32 actions = r.u16_at(at + l.actions_field);
    const u32 assets = r.u16_at(at + l.user_assets_field);
    const u32 triggers = r.u16_at(at + l.user_triggers_field);
    const u32 params = l.params_field == 0 ? 0 : r.u16_at(at + l.params_field);
    const std::size_t asset_indices =
        at + l.user_header + std::size_t{call_tables} * l.call_table_size + std::size_t{actions} * l.action_size;
    const std::size_t trigger_indices = asset_indices + std::size_t{assets} * 4;
    std::size_t p = trigger_indices + std::size_t{triggers} * 4 + std::size_t{assets} * params * 4;
    r.require(asset_indices, p - asset_indices + 4, lib + " user \"" + u.name + "\" tables");
    ctx.spend(p - at, "user tables");

    // Indices pick from the shared pools only when the user keeps no copies.
    const auto pooled = [&](std::size_t indices, u32 count, u32 pool, std::size_t size, std::string_view noun) {
      if (r.u32_at(p) != SharedPoolMarker) {
        r.require(p, std::size_t{count} * size, lib + " user \"" + u.name + "\" " + std::string(noun) + "s");
        p += std::size_t{count} * size;
        return;
      }
      p += 4;
      for (u32 k = 0; k < count; ++k) {
        const u32 index = r.u32_at(indices + std::size_t{k} * 4);
        if (index >= pool) {
          out.errors.push_back(lib + " user \"" + u.name + "\" picks shared " + std::string(noun) + " " +
                               std::to_string(index) + " of " + std::to_string(pool));
          return;
        }
      }
    };
    pooled(asset_indices, assets, shared_assets, l.asset_size, l.asset);
    pooled(trigger_indices, triggers, shared_triggers, l.trigger_size, "action trigger");
    // A zero trailer sends name lookups to the file's string table; anything
    // else starts strings of the user's own.
    r.require(p, 4, lib + " user \"" + u.name + "\" trailer");
    if (l.trailer_offset && r.u32_at(at + 0xC) != p - at)
      out.errors.push_back(lib + " user \"" + u.name + "\" places its trailer at " + fmt_hex(r.u32_at(at + 0xC)) +
                           " but its tables end at " + fmt_hex(p - at));

    totals[0] += call_tables;
    totals[1] += assets;
    totals[2] += actions;
    totals[3] += triggers;
    if (ctx.list_members)
      rows.push_back(u.name + ": " + count_of(call_tables, "call table") + ", " + count_of(assets, l.asset) + ", " +
                     count_of(actions, "action") + ", " + count_of(triggers, "action trigger") +
                     (u.base.empty() ? "" : " (based on " + u.base + ")"));
    users.push_back(std::move(u));
  }

  std::set<std::string_view> names;
  std::size_t aliases = 0;
  for (const User& u : users)
    names.insert(u.name);
  for (const User& u : users) {
    aliases += u.alias;
    if (u.base.empty() ? !u.alias : names.contains(u.base))
      continue;
    if (u.alias)
      out.errors.push_back(lib + " user \"" + u.name + "\" aliases missing user \"" + u.base + "\"");
    else
      out.warnings.push_back(lib + " user \"" + u.name + "\" is based on missing user \"" + u.base + "\"");
  }

  s.add("Users", std::to_string(users.size()) + " (" + count_of(aliases, "alias", "aliases") + ")");
  s.add("Call Tables", std::to_string(totals[0]));
  s.add(std::string(l.assets_title), std::to_string(totals[1]));
  s.add("Actions", std::to_string(totals[2]));
  s.add("Action Triggers", std::to_string(totals[3]));

  if (rows.empty())
    return;
  Section& list = out.section(std::string(l.name) + " Users");
  for (const std::string& row : rows)
    list.add("", row);
}

}  // namespace nnfck::pooled_link
