#pragma once

#include <filesystem>
#include <vector>

#include "core/bytes.hpp"

namespace nnfck {

// Opens the file read-only and loads it whole; nnfck never writes to inputs.
std::vector<u8> read_file(const std::filesystem::path& path);

}  // namespace nnfck
