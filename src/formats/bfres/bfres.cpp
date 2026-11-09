#include "formats/bfres/bfres.hpp"

#include "formats/bfres/common.hpp"

namespace nnfck::bfres {

namespace {

constexpr std::size_t MinSize = 0x20;

}  // namespace

bool probe(Bytes data) {
  return ByteReader(data, Endian::Big).magic_at(0, "FRES");
}

// Switch files pad the signature to eight bytes with spaces; Wii U files put
// the version there instead.
void analyze(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(Name);
  ByteReader r(data, Endian::Big);
  r.require(0, MinSize, "BFRES header");
  if (r.magic_at(4, "    "))
    analyze_nx(r, out, ctx);
  else
    analyze_cafe(r, out, ctx);
}

}  // namespace nnfck::bfres
