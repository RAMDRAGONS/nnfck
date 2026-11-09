#pragma once

#include <string_view>

#include "formats/format.hpp"

namespace nnfck::havok {

inline constexpr std::string_view Name = "Havok Packfile";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::havok
