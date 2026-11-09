#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/bytes.hpp"

// A small XML reader for the text resources that SDK tools write: UTF-8,
// elements, attributes, character data, comments, CDATA and processing
// instructions. Document type declarations are skipped, not interpreted, so
// no entity beyond the predefined and numeric ones is ever expanded.
namespace nnfck::xml {

struct Element {
  std::string name;
  std::vector<std::pair<std::string, std::string>> attributes;
  std::string text;
  std::vector<Element> children;

  // The name without its namespace prefix.
  std::string_view local_name() const;
  const std::string* attribute(std::string_view name) const;
  std::string attribute_or(std::string_view name, std::string_view fallback = {}) const;
  const Element* child(std::string_view local) const;
  std::vector<const Element*> children_named(std::string_view local) const;
};

struct Limits {
  int max_depth = 256;
  // Elements and attributes together.
  std::size_t max_nodes = 1u << 22;
};

// Parses a whole document. Throws ParseError naming the offset of the first
// malformed construct.
Element parse(Bytes data, const Limits& limits = {});

// The root element's name, found without parsing past its start tag. Empty
// when `data` does not open like an XML document.
std::string root_name(Bytes data);

}  // namespace nnfck::xml
