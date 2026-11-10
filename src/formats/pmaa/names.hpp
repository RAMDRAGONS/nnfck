#pragma once

#include <string>
#include <string_view>

#include "core/bytes.hpp"

// Parameter archives store names only as CRC-32 hashes; the names that resolve
// them ship in a JSON data file.
namespace nnfck::pmaa {

inline constexpr std::string_view NamesFile = "pmaa_names.json";

// The name a parameter archive hashed into `hash`, or null when the hash
// belongs to no known name.
const std::string* name_of(u32 hash);

// The known name, or the hash in hex.
std::string display_name(u32 hash);

// Why the name list could not be used, or empty once it loaded.
const std::string& names_problem();

}  // namespace nnfck::pmaa
