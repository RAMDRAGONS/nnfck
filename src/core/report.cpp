#include "core/report.hpp"

#include <algorithm>

namespace nnfck {

void Analysis::absorb(Analysis&& inner) {
  for (auto& s : inner.sections)
    sections.push_back(std::move(s));
  for (auto& w : inner.warnings)
    warnings.push_back(std::move(w));
  for (auto& e : inner.errors)
    errors.push_back(std::move(e));
}

void print_analysis(std::ostream& out, std::string_view path, const Analysis& analysis) {
  out << "File:   " << path << '\n';
  out << "Format: " << analysis.format << '\n';

  for (const Section& section : analysis.sections) {
    out << '\n' << '[' << section.title() << "]\n";

    const auto indent = [](const Field& f) { return 2 + static_cast<std::size_t>(std::max(f.depth, 0)) * 2; };
    std::size_t width = 0;
    for (const Field& f : section.fields())
      if (!f.key.empty())
        width = std::max(width, indent(f) + f.key.size());

    for (const Field& f : section.fields()) {
      if (f.key.empty()) {
        out << std::string(indent(f), ' ') << f.value << '\n';
        continue;
      }
      const std::size_t used = indent(f) + f.key.size();
      out << std::string(indent(f), ' ') << f.key << ':';
      if (!f.value.empty())
        out << std::string(width - used + 1, ' ') << f.value;
      out << '\n';
    }
  }

  if (!analysis.warnings.empty()) {
    out << '\n';
    for (const auto& w : analysis.warnings)
      out << "Warning: " << w << '\n';
  }
  if (!analysis.errors.empty()) {
    out << '\n';
    for (const auto& e : analysis.errors)
      out << "Error: " << e << '\n';
  }
}

std::string fmt_hex(u64 value, int min_digits) {
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string out;
  do {
    out.insert(out.begin(), digits[value & 0xF]);
    value >>= 4;
  } while (value != 0);
  if (static_cast<int>(out.size()) < min_digits)
    out.insert(0, static_cast<std::size_t>(min_digits) - out.size(), '0');
  return "0x" + out;
}

std::string fmt_size(u64 value) {
  return fmt_hex(value) + " (" + std::to_string(value) + (value == 1 ? " byte)" : " bytes)");
}

std::string fmt_hex_bytes(Bytes bytes) {
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string out;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i != 0)
      out.push_back(' ');
    out.push_back(digits[bytes[i] >> 4]);
    out.push_back(digits[bytes[i] & 0xF]);
  }
  return out;
}

std::string fmt_quoted(Bytes bytes) {
  std::string out = "\"";
  for (const u8 c : bytes) {
    if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') {
      out.push_back(static_cast<char>(c));
    } else {
      static constexpr char digits[] = "0123456789ABCDEF";
      out += "\\x";
      out.push_back(digits[c >> 4]);
      out.push_back(digits[c & 0xF]);
    }
  }
  out.push_back('"');
  return out;
}

std::string fmt_bool(bool value) {
  return value ? "Yes" : "No";
}

}  // namespace nnfck
