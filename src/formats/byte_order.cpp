#include "formats/byte_order.hpp"

#include "core/report.hpp"

namespace nnfck {

Endian read_bom(const ByteReader& r, std::size_t offset) {
  const u8 a = r.u8_at(offset), b = r.u8_at(offset + 1);
  if (a == 0xFE && b == 0xFF)
    return Endian::Big;
  if (a == 0xFF && b == 0xFE)
    return Endian::Little;
  throw ParseError("invalid byte order mark " + fmt_hex_bytes(r.data().subspan(offset, 2)));
}

}  // namespace nnfck
