#include "formats/fres/resources.hpp"

// Switch material animations, which merged the Wii U shader parameter,
// texture pattern and material visibility animations.
namespace nnfck::fres::fmaa {

std::string describe_nx(const ByteReader& r, const NxLayout& l, std::size_t at) {
  std::vector<std::string> parts{frames(r.s32_at(at + l.material_frames)),
                                 count_of(r.u16_at(at + l.material_per_material), "material animation"),
                                 count_of(r.u16_at(at + l.material_shader_param), "shader parameter animation"),
                                 count_of(r.u16_at(at + l.material_tex_pattern), "texture pattern animation"),
                                 count_of(r.u16_at(at + l.material_visibility), "visibility animation"),
                                 count_of(r.u16_at(at + l.material_textures), "texture"),
                                 count_of(r.u16_at(at + l.material_curves), "curve")};
  add_anim_flags(parts, r.u16_at(at + l.material_flag));
  return join(parts);
}

}  // namespace nnfck::fres::fmaa
