#pragma once

#include <string_view>

#include "formats/format.hpp"

namespace nnfck::sarc {

inline constexpr std::string_view Name = "Sharc Archive";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::sarc
