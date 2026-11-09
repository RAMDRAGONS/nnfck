#pragma once

#include <string_view>

#include "formats/format.hpp"

namespace nnfck::bntx {

inline constexpr std::string_view Name = "Binary Nintendo Texture";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::bntx
