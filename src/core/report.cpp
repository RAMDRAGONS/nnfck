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

namespace {

void escape_byte(std::string& out, u8 c) {
  static constexpr char digits[] = "0123456789ABCDEF";
  out += "\\x";
  out.push_back(digits[c >> 4]);
  out.push_back(digits[c & 0xF]);
}

// Length of the well-formed UTF-8 sequence at `s[i]`, or 0 when it is malformed,
// overlong, a surrogate, beyond U+10FFFF, or a C1 control (U+0080 to U+009F),
// which some terminals act on as escape introducers.
std::size_t printable_utf8_length(std::string_view s, std::size_t i) {
  const auto b = [&](std::size_t k) { return static_cast<u8>(s[i + k]); };
  const u8 lead = b(0);
  std::size_t length;
  u32 cp;
  if (lead >= 0xC2 && lead <= 0xDF) {
    length = 2;
    cp = lead & 0x1Fu;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    length = 3;
    cp = lead & 0x0Fu;
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    length = 4;
    cp = lead & 0x07u;
  } else {
    return 0;
  }
  if (s.size() - i < length)
    return 0;
  for (std::size_t k = 1; k < length; ++k) {
    if ((b(k) & 0xC0) != 0x80)
      return 0;
    cp = cp << 6 | (b(k) & 0x3Fu);
  }
  static constexpr u32 MinForLength[] = {0, 0, 0x80, 0x800, 0x10000};
  if (cp < MinForLength[length] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) || cp <= 0x9F)
    return 0;
  return length;
}

}  // namespace

std::string sanitize(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    const u8 c = static_cast<u8>(s[i]);
    if (c >= 0x20 && c < 0x7F) {
      out.push_back(static_cast<char>(c));
      ++i;
    } else if (const std::size_t n = c >= 0x80 ? printable_utf8_length(s, i) : 0; n != 0) {
      out.append(s.substr(i, n));
      i += n;
    } else {
      escape_byte(out, c);
      ++i;
    }
  }
  return out;
}

void print_analysis(std::ostream& out, std::string_view path, const Analysis& analysis) {
  out << "File:   " << sanitize(path) << '\n';
  out << "Format: " << sanitize(analysis.format) << '\n';

  for (const Section& section : analysis.sections) {
    out << '\n' << '[' << sanitize(section.title()) << "]\n";

    const auto indent = [](const Field& f) { return 2 + static_cast<std::size_t>(std::max(f.depth, 0)) * 2; };
    std::vector<std::string> keys;
    keys.reserve(section.fields().size());
    std::size_t width = 0;
    for (const Field& f : section.fields()) {
      keys.push_back(sanitize(f.key));
      if (!f.key.empty())
        width = std::max(width, indent(f) + keys.back().size());
    }

    for (std::size_t i = 0; i < section.fields().size(); ++i) {
      const Field& f = section.fields()[i];
      if (f.key.empty()) {
        out << std::string(indent(f), ' ') << sanitize(f.value) << '\n';
        continue;
      }
      const std::size_t used = indent(f) + keys[i].size();
      out << std::string(indent(f), ' ') << keys[i] << ':';
      if (!f.value.empty())
        out << std::string(width - used + 1, ' ') << sanitize(f.value);
      out << '\n';
    }
  }

  if (!analysis.warnings.empty()) {
    out << '\n';
    for (const auto& w : analysis.warnings)
      out << "Warning: " << sanitize(w) << '\n';
  }
  if (!analysis.errors.empty()) {
    out << '\n';
    for (const auto& e : analysis.errors)
      out << "Error: " << sanitize(e) << '\n';
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
