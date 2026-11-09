#include "formats/fres/resources.hpp"

namespace nnfck::fres::fska {

std::string describe_cafe(const ByteReader& r, std::size_t at, bool legacy) {
  const s32 frame_count = legacy ? r.u16_at(at + 0x10) : r.s32_at(at + 0x10);
  const u16 bone_anims = r.u16_at(at + (legacy ? 0x12 : 0x14));
  const s32 curves = r.s32_at(at + (legacy ? 0x14 : 0x18));
  std::vector<std::string> parts{frames(frame_count), count_of(bone_anims, "bone animation"),
                                 count_of(static_cast<u32>(curves), "curve")};
  add_anim_flags(parts, r.u32_at(at + 0x0C));
  return join(parts);
}

std::string describe_nx(const ByteReader& r, const NxLayout& l, std::size_t at) {
  std::vector<std::string> parts{frames(r.s32_at(at + l.skeletal_frames)),
                                 count_of(r.u16_at(at + l.skeletal_bone_anims), "bone animation"),
                                 count_of(static_cast<u32>(r.s32_at(at + l.skeletal_curves)), "curve")};
  add_anim_flags(parts, r.u16_at(at + l.skeletal_flag));
  return join(parts);
}

}  // namespace nnfck::fres::fska
