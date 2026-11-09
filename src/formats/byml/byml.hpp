#pragma once

#include <string_view>

#include "formats/format.hpp"

namespace nnfck::byml {

inline constexpr std::string_view Name = "Binary YAML";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::byml
