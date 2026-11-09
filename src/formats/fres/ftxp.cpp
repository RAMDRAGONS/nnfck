#include "formats/fres/resources.hpp"

namespace nnfck::fres::ftxp {

std::string describe_cafe(const ByteReader& r, std::size_t at, bool legacy) {
  const s32 frame_count = legacy ? r.u16_at(at + 0x0E) : r.s32_at(at + 0x10);
  const std::size_t counts = at + (legacy ? 0x10 : 0x14);
  std::vector<std::string> parts{frames(frame_count), count_of(r.u16_at(counts + 2), "material animation"),
                                 count_of(static_cast<u32>(r.s32_at(counts + 4)), "pattern animation"),
                                 count_of(r.u16_at(counts), "texture"),
                                 count_of(static_cast<u32>(r.s32_at(counts + 8)), "curve")};
  add_anim_flags(parts, r.u16_at(at + 0x0C));
  return join(parts);
}

}  // namespace nnfck::fres::ftxp
