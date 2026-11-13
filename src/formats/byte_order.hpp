#pragma once

#include <cstddef>

#include "core/bytes.hpp"

namespace nnfck {

// Reads a byte order mark stored as the bytes FE FF (big-endian) or FF FE
// (little-endian).
Endian read_bom(const ByteReader& r, std::size_t offset);

}  // namespace nnfck
