#pragma once

#include <string_view>

#include "formats/format.hpp"

// nw4f_3dif intermediate files: the XML sources each resource inside a
// BFRES is converted from. The format line names the resource the root
// holds; binary variants append their streams after the XML.
namespace nnfck::fres::intermediate {

// Used only when a file fails before its resource type is known.
inline constexpr std::string_view Name = "Cafe Intermediate";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::fres::intermediate
