#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/bytes.hpp"

// A small JSON reader for nnfck's own data files.
namespace nnfck::json {

struct Value {
  enum class Kind { Null, Bool, Number, String, Array, Object };

  Kind kind = Kind::Null;
  bool boolean = false;
  double number = 0;
  std::string string;
  // Array elements, or object member values in document order.
  std::vector<Value> items;
  // Object member names, parallel to `items`.
  std::vector<std::string> keys;

  bool is(Kind k) const { return kind == k; }
  // The member named `key` of an object, or null.
  const Value* member(std::string_view key) const;
};

inline constexpr int MaxDepth = 64;

// Throws ParseError on malformed input or nesting beyond MaxDepth.
Value parse(Bytes data);

}  // namespace nnfck::json
