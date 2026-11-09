#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core/error.hpp"

namespace nnfck {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s16 = std::int16_t;
using s32 = std::int32_t;

using Bytes = std::span<const u8>;

enum class Endian { Big, Little };

constexpr std::string_view endian_name(Endian e) {
  return e == Endian::Big ? "Big endian" : "Little endian";
}

// Every read is bounds-checked against the view so that malformed input
// surfaces as a ParseError naming the offending offset instead of UB.
class ByteReader {
public:
  ByteReader(Bytes data, Endian endian) : m_data{data}, m_endian{endian} {}

  Bytes data() const { return m_data; }
  std::size_t size() const { return m_data.size(); }
  Endian endian() const { return m_endian; }
  void set_endian(Endian e) { m_endian = e; }

  bool has(std::size_t offset, std::size_t length) const {
    return offset <= m_data.size() && length <= m_data.size() - offset;
  }

  void require(std::size_t offset, std::size_t length, std::string_view what) const {
    if (!has(offset, length))
      throw ParseError(std::string(what) + " at offset " + hex_offset(offset) + " exceeds data size " +
                       hex_offset(m_data.size()));
  }

  u8 u8_at(std::size_t offset) const {
    require(offset, 1, "u8 read");
    return m_data[offset];
  }

  u16 u16_at(std::size_t offset) const {
    require(offset, 2, "u16 read");
    const u16 a = m_data[offset], b = m_data[offset + 1];
    return m_endian == Endian::Big ? u16(a << 8 | b) : u16(b << 8 | a);
  }

  u32 u32_at(std::size_t offset) const {
    require(offset, 4, "u32 read");
    u32 v = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      const std::size_t idx = m_endian == Endian::Big ? offset + i : offset + 3 - i;
      v = v << 8 | m_data[idx];
    }
    return v;
  }

  u64 u64_at(std::size_t offset) const {
    require(offset, 8, "u64 read");
    u64 v = 0;
    for (std::size_t i = 0; i < 8; ++i) {
      const std::size_t idx = m_endian == Endian::Big ? offset + i : offset + 7 - i;
      v = v << 8 | m_data[idx];
    }
    return v;
  }

  s16 s16_at(std::size_t offset) const { return static_cast<s16>(u16_at(offset)); }
  s32 s32_at(std::size_t offset) const { return static_cast<s32>(u32_at(offset)); }

  bool magic_at(std::size_t offset, std::string_view magic) const {
    if (!has(offset, magic.size()))
      return false;
    for (std::size_t i = 0; i < magic.size(); ++i)
      if (m_data[offset + i] != static_cast<u8>(magic[i]))
        return false;
    return true;
  }

  std::string cstring_at(std::size_t offset, std::size_t max_length) const {
    require(offset, 1, "string read");
    std::string s;
    for (std::size_t i = offset; i < m_data.size() && s.size() < max_length; ++i) {
      if (m_data[i] == 0)
        return s;
      s.push_back(static_cast<char>(m_data[i]));
    }
    throw ParseError("unterminated string at offset " + hex_offset(offset));
  }

  static std::string hex_offset(std::size_t v);

private:
  Bytes m_data;
  Endian m_endian;
};

inline std::string ByteReader::hex_offset(std::size_t v) {
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string out;
  do {
    out.insert(out.begin(), digits[v & 0xF]);
    v >>= 4;
  } while (v != 0);
  return "0x" + out;
}

}  // namespace nnfck
