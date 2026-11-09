#include "formats/fres/resources.hpp"

// Wii U shader parameter animations, which also carry the color and texture
// SRT animations a BFRES indexes separately.
namespace nnfck::fres::fshu {

std::string describe_cafe(const ByteReader& r, std::size_t at, bool legacy) {
  const s32 frame_count = legacy ? r.u16_at(at + 0x10) : r.s32_at(at + 0x10);
  const u16 mat_anims = r.u16_at(at + (legacy ? 0x12 : 0x14));
  const s32 param_anims = r.s32_at(at + (legacy ? 0x14 : 0x18));
  const s32 curves = r.s32_at(at + (legacy ? 0x18 : 0x1C));
  std::vector<std::string> parts{frames(frame_count), count_of(mat_anims, "material animation"),
                                 count_of(static_cast<u32>(param_anims), "parameter animation"),
                                 count_of(static_cast<u32>(curves), "curve")};
  add_anim_flags(parts, r.u32_at(at + 0x0C));
  return join(parts);
}

}  // namespace nnfck::fres::fshu
