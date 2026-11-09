#include "formats/fres/resources.hpp"

namespace nnfck::fres::fsha {

std::string describe_cafe(const ByteReader& r, std::size_t at, bool legacy) {
  std::vector<std::string> parts;
  if (legacy)
    parts = {frames(r.u16_at(at + 0x0E)), count_of(r.u16_at(at + 0x10), "vertex shape animation"),
             count_of(r.u8_at(at + 0x12), "key shape animation"), count_of(r.u8_at(at + 0x13), "curve")};
  else
    parts = {frames(r.s32_at(at + 0x10)), count_of(r.u16_at(at + 0x14), "vertex shape animation"),
             count_of(r.u16_at(at + 0x16), "key shape animation"), count_of(r.u16_at(at + 0x18), "curve")};
  add_anim_flags(parts, r.u16_at(at + 0x0C));
  return join(parts);
}

std::string describe_nx(const ByteReader& r, const NxLayout& l, std::size_t at) {
  std::vector<std::string> parts{frames(r.s32_at(at + l.shape_frames)),
                                 count_of(r.u16_at(at + l.shape_vertex_anims), "vertex shape animation"),
                                 count_of(r.u16_at(at + l.shape_key_anims), "key shape animation"),
                                 count_of(r.u16_at(at + l.shape_curves), "curve")};
  add_anim_flags(parts, r.u16_at(at + l.shape_flag));
  return join(parts);
}

}  // namespace nnfck::fres::fsha
