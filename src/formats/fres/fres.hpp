#pragma once

#include <string_view>

#include "formats/format.hpp"

namespace nnfck::fres {

inline constexpr std::string_view Name = "Binary Cafe Resource";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::fres
