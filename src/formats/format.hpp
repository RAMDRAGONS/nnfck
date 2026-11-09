#pragma once

#include <string_view>

#include "core/bytes.hpp"
#include "core/report.hpp"

namespace nnfck {

// Container formats analyze their payload through the registry again; depth
// bounds that recursion so crafted self-nesting input cannot exhaust the stack.
struct Context {
  int depth = 0;
  static constexpr int MaxDepth = 8;
};

struct FormatHandler {
  std::string_view name;
  bool (*probe)(Bytes data);
  void (*analyze)(Bytes data, Analysis& out, const Context& ctx);
};

const FormatHandler* identify(Bytes data);

// Analyzes `data` with whichever handler claims it. Unclaimed data yields an
// "Unknown" analysis with a short hex preview rather than an error.
Analysis analyze(Bytes data, const Context& ctx);

}  // namespace nnfck
