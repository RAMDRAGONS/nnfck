#pragma once

#include <string_view>

#include "formats/format.hpp"

namespace nnfck::flyt {

inline constexpr std::string_view Name = "Binary Cafe Layout";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

// The XML source the binary is converted from.
inline constexpr std::string_view TextName = "Cafe Layout";

bool probe_text(Bytes data);
void analyze_text(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::flyt

namespace nnfck::flan {

inline constexpr std::string_view Name = "Binary Cafe Layout Animation";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

// The XML source the binary is converted from.
inline constexpr std::string_view TextName = "Cafe Layout Animation";

bool probe_text(Bytes data);
void analyze_text(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::flan

namespace nnfck::fcpx {

inline constexpr std::string_view Name = "Binary Cafe Complex Font";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

// The XML source the binary is converted from.
inline constexpr std::string_view TextName = "Cafe Complex Font";

bool probe_text(Bytes data);
void analyze_text(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::fcpx

namespace nnfck::flim {

inline constexpr std::string_view Name = "Cafe Layout Image";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::flim
