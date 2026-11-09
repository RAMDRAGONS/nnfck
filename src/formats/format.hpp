#pragma once

#include <string_view>

#include "core/bytes.hpp"
#include "core/report.hpp"

namespace nnfck {

// Total bytes one input may cause to be analyzed or decompressed. Containers can
// list the same bytes many times over and nest within each other, so depth alone
// does not stop crafted input from multiplying the work at every level.
struct Budget {
  u64 remaining;

  static Budget for_input(std::size_t size) { return Budget{(u64{1} << 30) + u64(size) * 16}; }

  bool charge(u64 bytes) {
    if (bytes > remaining)
      return false;
    remaining -= bytes;
    return true;
  }
};

// Container formats analyze their payload through the registry again; depth
// bounds that recursion so crafted self-nesting input cannot exhaust the stack.
struct Context {
  int depth = 0;
  // Unlimited when null.
  Budget* budget = nullptr;
  static constexpr int MaxDepth = 8;

  Context nested() const { return Context{depth + 1, budget}; }
  bool charge(u64 bytes) const { return budget == nullptr || budget->charge(bytes); }
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
