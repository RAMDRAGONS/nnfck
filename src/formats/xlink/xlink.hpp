#pragma once

#include <string_view>

#include "formats/format.hpp"

// XLink resource databases ("XLNK"): one file per system, either ELink2
// (effects) or SLink2 (sound), sharing a single layout.
namespace nnfck::xlink {

inline constexpr std::string_view Name = "Binary XLink Database";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::xlink

// EffectLink databases from before XLink: "BLNK", holding effect records per
// user, and "eflk", holding call tables, effects and actions per user.
namespace nnfck::elink {

inline constexpr std::string_view Name = "Binary EffectLink Database";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::elink

// SoundLink databases ("SLNK") from before XLink: per user, call tables that
// play sound assets and actions with their triggers.
namespace nnfck::slink {

inline constexpr std::string_view Name = "Binary SoundLink Database";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::slink
