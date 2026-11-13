#pragma once

#include <cstddef>
#include <string_view>

#include "formats/format.hpp"

// The 3DS layout shared by EffectLink and SoundLink databases: the file
// header counts two shared pools, assets (effects for EffectLink) and action
// triggers, which follow it, then one offset per user and the string table.
// Users either hold their own tables, picking pool entries by index, or alias
// another user by name.
namespace nnfck::pooled_link {

struct Layout {
  std::string_view name;     // Format name, titling the user list.
  std::string_view library;  // Prefix for messages, such as "EffectLink".
  std::string_view asset;    // Singular noun for pool assets, such as "effect".
  std::string_view assets_title;

  std::size_t header;
  std::size_t users_field;
  std::size_t assets_field;
  std::size_t triggers_field;
  std::size_t asset_size;
  std::size_t trigger_size;

  // Each user header holds u16 counts at these offsets; params_field is 0
  // where users keep no per-asset parameters.
  std::size_t user_header;
  std::size_t call_tables_field;
  std::size_t actions_field;
  std::size_t user_assets_field;
  std::size_t user_triggers_field;
  std::size_t params_field;
  std::size_t call_table_size;
  std::size_t action_size;
  // Whether the user header's 0xC field holds the offset of its trailer.
  bool trailer_offset;
};

void analyze(const ByteReader& r, Section& s, Analysis& out, const Context& ctx, const Layout& l);

}  // namespace nnfck::pooled_link
