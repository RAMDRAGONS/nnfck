#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

// Data files that ship alongside nnfck, such as name lists for hashed
// formats.
namespace nnfck::data {

// Environment variable naming a directory searched before the defaults.
inline constexpr const char* DirVariable = "NNFCK_DATA_DIR";

// Searches $NNFCK_DATA_DIR, the executable's directory, then the install
// layout's ../share/nnfck beside it. Empty when no candidate exists.
std::optional<std::filesystem::path> find(std::string_view file);

}  // namespace nnfck::data
