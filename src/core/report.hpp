#pragma once

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/bytes.hpp"

namespace nnfck {

struct Field {
  std::string key;
  std::string value;
  int depth;
};

class Section {
public:
  explicit Section(std::string title) : m_title{std::move(title)} {}

  // An empty key prints the value alone as a preformatted row.
  Section& add(std::string key, std::string value, int depth = 0) {
    m_fields.push_back({std::move(key), std::move(value), depth});
    return *this;
  }

  const std::string& title() const { return m_title; }
  const std::vector<Field>& fields() const { return m_fields; }

private:
  std::string m_title;
  std::vector<Field> m_fields;
};

// Result of analyzing one blob. `format` is the one-line identification, which
// container formats build by prefixing their own name to their payload's.
struct Analysis {
  std::string format;
  std::vector<Section> sections;
  std::vector<std::string> warnings;
  std::vector<std::string> errors;

  Section& section(std::string title) { return sections.emplace_back(std::move(title)); }

  void absorb(Analysis&& inner);
};

// Escapes control characters, C1 controls and malformed UTF-8 as \xNN so text
// taken from input files cannot drive the terminal.
std::string sanitize(std::string_view s);

// Every string is passed through sanitize() on its way out.
void print_analysis(std::ostream& out, std::string_view path, const Analysis& analysis);

std::string fmt_hex(u64 value, int min_digits = 0);
std::string fmt_size(u64 value);
std::string fmt_hex_bytes(Bytes bytes);
std::string fmt_quoted(Bytes bytes);
std::string fmt_bool(bool value);

}  // namespace nnfck
