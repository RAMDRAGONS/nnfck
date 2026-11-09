#pragma once

#include <string_view>

#include "formats/format.hpp"

namespace nnfck::bfres {

inline constexpr std::string_view Name = "NintendoWare Binary Cafe Resource";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::bfres
