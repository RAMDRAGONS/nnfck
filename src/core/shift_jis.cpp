#include "core/shift_jis.hpp"

#include <array>

#include "core/utf8.hpp"

namespace nnfck {

namespace {

constexpr u8 FirstTrail = 0x40, LastTrail = 0xFC;
constexpr std::size_t Leads = 60, Trails = LastTrail - FirstTrail + 1;

constexpr std::array<u16, Leads * Trails> DoubleByte = {
#include "core/shift_jis_table.inc"
};

// Lead bytes 0x81 to 0x9F and 0xE0 to 0xFC, numbered without the gap.
int lead_slot(u8 b) {
  if (b >= 0x81 && b <= 0x9F)
    return b - 0x81;
  if (b >= 0xE0 && b <= 0xFC)
    return b - 0xE0 + 0x1F;
  return -1;
}

}  // namespace

std::string shift_jis_to_utf8(std::string_view s) {
  std::string out;
  out.reserve(s.size() * 3 / 2);
  for (std::size_t i = 0; i < s.size(); ++i) {
    const u8 b = static_cast<u8>(s[i]);
    if (b < 0x80) {
      out.push_back(static_cast<char>(b));
      continue;
    }
    // Halfwidth katakana take a single byte.
    if (b >= 0xA1 && b <= 0xDF) {
      append_utf8(out, 0xFF61 + (b - 0xA1));
      continue;
    }
    const int slot = lead_slot(b);
    if (slot >= 0 && i + 1 < s.size()) {
      const u8 t = static_cast<u8>(s[i + 1]);
      const u16 cp = t >= FirstTrail && t <= LastTrail ? DoubleByte[static_cast<std::size_t>(slot) * Trails + (t - FirstTrail)] : 0;
      if (cp != 0) {
        append_utf8(out, cp);
        ++i;
        continue;
      }
    }
    out.push_back(static_cast<char>(b));
  }
  return out;
}

}  // namespace nnfck
