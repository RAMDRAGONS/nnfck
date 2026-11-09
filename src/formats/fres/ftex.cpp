#include "formats/fres/resources.hpp"
#include "formats/gx2/gx2.hpp"

// Wii U textures: a GX2Texture after the signature, the GX2Surface first,
// then the view range, channel selection and texture registers.
namespace nnfck::fres::ftex {

std::string describe_cafe(const ByteReader& r, std::size_t at) {
  const u32 dim = r.u32_at(at + 0x04);
  const u32 width = r.u32_at(at + 0x08);
  const u32 height = r.u32_at(at + 0x0C);
  const u32 depth = r.u32_at(at + 0x10);
  const u32 mips = r.u32_at(at + 0x14);
  const u32 format = r.u32_at(at + 0x18);
  const u32 aa = r.u32_at(at + 0x1C);
  const u32 tile = r.u32_at(at + 0x34);

  std::string size = std::to_string(width) + "x" + std::to_string(height);
  if (depth > 1)
    size += "x" + std::to_string(depth);
  std::vector<std::string> parts{size, gx2::surface_format_name(format), count_of(mips == 0 ? 1 : mips, "mip"),
                                 gx2::surface_dim_name(dim), gx2::tile_mode_name(tile)};
  if (aa != 0)
    parts.push_back(gx2::aa_mode_name(aa) + " AA");
  return join(parts);
}

}  // namespace nnfck::fres::ftex
