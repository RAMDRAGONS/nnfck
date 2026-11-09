#pragma once

#include <string_view>

#include "formats/format.hpp"

// Zstandard: one or more frames, optionally interleaved with skippable
// frames that carry application data instead of compressed content.
namespace nnfck::zstd {

inline constexpr std::string_view Name = "Zstandard";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::zstd
