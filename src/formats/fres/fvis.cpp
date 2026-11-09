#include "formats/fres/resources.hpp"

namespace nnfck::fres::fvis {

namespace {

constexpr u32 VisibilityTypeShift = 8;

}  // namespace

std::string describe_cafe(const ByteReader& r, Analysis& out, std::size_t at, bool material,
                          const std::string& name, bool legacy) {
  const u16 flag = r.u16_at(at + 0x0C);
  if (((flag >> VisibilityTypeShift) & 1) != (material ? 1 : 0))
    out.warnings.push_back("BFRES visibility animation \"" + name + "\" is flagged as " +
                           (material ? "bone" : "material") + " visibility but listed as " +
                           (material ? "material" : "bone") + " visibility");
  const s32 frame_count = legacy ? r.u16_at(at + 0x0E) : r.s32_at(at + 0x10);
  const std::size_t counts = at + (legacy ? 0x10 : 0x14);
  std::vector<std::string> parts{frames(frame_count), count_of(r.u16_at(counts), "animation"),
                                 count_of(r.u16_at(counts + 2), "curve")};
  add_anim_flags(parts, flag);
  return join(parts);
}

std::string describe_nx(const ByteReader& r, const NxLayout& l, std::size_t at) {
  std::vector<std::string> parts{frames(r.s32_at(at + l.bone_vis_frames)),
                                 count_of(r.u16_at(at + l.bone_vis_anims), "animation"),
                                 count_of(r.u16_at(at + l.bone_vis_curves), "curve")};
  add_anim_flags(parts, r.u16_at(at + l.bone_vis_flag));
  return join(parts);
}

}  // namespace nnfck::fres::fvis
