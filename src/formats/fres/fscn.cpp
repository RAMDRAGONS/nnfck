#include "formats/fres/resources.hpp"

// Scene animations: camera, light and fog animations.
namespace nnfck::fres::fscn {

std::string describe_cafe(const ByteReader& r, Analysis& out, std::size_t at, const std::string& name,
                          bool legacy) {
  if (!legacy)
    return join({count_of(r.u16_at(at + 0x0E), "camera animation"), count_of(r.u16_at(at + 0x10), "light animation"),
                 count_of(r.u16_at(at + 0x12), "fog animation")});
  // Legacy scenes keep 8-bit counts ahead of the camera, light and fog
  // index groups; each count must agree with its group.
  const char* const kinds[] = {"camera", "light", "fog"};
  std::vector<std::string> parts;
  for (std::size_t i = 0; i < 3; ++i) {
    const u8 declared = r.u8_at(at + 0x0E + i);
    expect_count(out, "scene animation \"" + name + "\"", std::string(kinds[i]) + " animations", declared,
                 cafe_dict_count(r, at + 0x14 + i * 4), "index group");
    parts.push_back(count_of(declared, std::string(kinds[i]) + " animation"));
  }
  return join(parts);
}

std::string describe_nx(const ByteReader& r, const NxLayout& l, std::size_t at) {
  return join({count_of(r.u16_at(at + l.scene_cameras), "camera animation"),
               count_of(r.u16_at(at + l.scene_lights), "light animation"),
               count_of(r.u16_at(at + l.scene_fogs), "fog animation")});
}

}  // namespace nnfck::fres::fscn
