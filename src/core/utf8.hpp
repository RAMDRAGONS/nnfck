#pragma once

#include <string>

#include "core/bytes.hpp"

namespace nnfck {

inline void append_utf8(std::string& out, u32 cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | cp >> 6));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | cp >> 12));
    out.push_back(static_cast<char>(0x80 | (cp >> 6 & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | cp >> 18));
    out.push_back(static_cast<char>(0x80 | (cp >> 12 & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp >> 6 & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

}  // namespace nnfck
