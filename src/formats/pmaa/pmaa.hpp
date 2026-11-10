#pragma once

#include <string_view>

#include "formats/format.hpp"

// agl parameter archives (signature PMAA, stored little-endian as the bytes
// "AAMP"): a tree of lists, objects and typed parameters, each keyed by the
// CRC-32 of its name. The type string after the header names what the archive
// configures, such as aglenv for environment objects, and with a leading b
// forms the file extension. Version 1 nests every node inline behind its
// size; version 2 indexes nodes through offset tables.
namespace nnfck::pmaa {

inline constexpr std::string_view Name = "Parameter Archive";

bool probe(Bytes data);
void analyze(Bytes data, Analysis& out, const Context& ctx);

// The XML source the binary is converted from, saved with the bare extension
// (.aglenv for .baglenv).
bool probe_text(Bytes data);
void analyze_text(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::pmaa
