#pragma once

#include <string_view>

#include "formats/format.hpp"

// EffectMaker emitter set sources, the XML each emitter set inside a PTCL is
// converted from. Wii U tools write an EmitterSet root, Switch tools an
// EmitterSetData root.
namespace nnfck::eset {

inline constexpr std::string_view Name = "Emitter Set";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::eset
