#include "formats/gx2/gx2.hpp"

#include <algorithm>
#include <iterator>

#include "core/report.hpp"

namespace nnfck::gx2 {

namespace {

struct Name {
  u32 value;
  const char* name;
};

// Where several names share a value, the texture-usable one is listed.
constexpr Name SurfaceFormats[] = {
    {0x000, "INVALID"},
    {0x001, "TC_R8_UNORM"},
    {0x002, "T_R4_G4_UNORM"},
    {0x005, "TCD_R16_UNORM"},
    {0x007, "TC_R8_G8_UNORM"},
    {0x008, "TCS_R5_G6_B5_UNORM"},
    {0x00A, "TC_R5_G5_B5_A1_UNORM"},
    {0x00B, "TC_R4_G4_B4_A4_UNORM"},
    {0x00C, "TC_A1_B5_G5_R5_UNORM"},
    {0x00F, "TC_R16_G16_UNORM"},
    {0x011, "T_R24_UNORM_X8"},
    {0x019, "TCS_R10_G10_B10_A2_UNORM"},
    {0x01A, "TCS_R8_G8_B8_A8_UNORM"},
    {0x01B, "TCS_A2_B10_G10_R10_UNORM"},
    {0x01F, "TC_R16_G16_B16_A16_UNORM"},
    {0x031, "T_BC1_UNORM"},
    {0x032, "T_BC2_UNORM"},
    {0x033, "T_BC3_UNORM"},
    {0x034, "T_BC4_UNORM"},
    {0x035, "T_BC5_UNORM"},
    {0x081, "T_NV12_UNORM"},
    {0x101, "TC_R8_UINT"},
    {0x105, "TC_R16_UINT"},
    {0x107, "TC_R8_G8_UINT"},
    {0x10D, "TC_R32_UINT"},
    {0x10F, "TC_R16_G16_UINT"},
    {0x111, "T_X24_G8_UINT"},
    {0x119, "TC_R10_G10_B10_A2_UINT"},
    {0x11A, "TC_R8_G8_B8_A8_UINT"},
    {0x11B, "TC_A2_B10_G10_R10_UINT"},
    {0x11C, "T_X32_G8_UINT_X24"},
    {0x11D, "TC_R32_G32_UINT"},
    {0x11F, "TC_R16_G16_B16_A16_UINT"},
    {0x122, "TC_R32_G32_B32_A32_UINT"},
    {0x201, "TC_R8_SNORM"},
    {0x205, "TC_R16_SNORM"},
    {0x207, "TC_R8_G8_SNORM"},
    {0x20F, "TC_R16_G16_SNORM"},
    {0x219, "T_R10_G10_B10_A2_SNORM"},
    {0x21A, "TC_R8_G8_B8_A8_SNORM"},
    {0x21F, "TC_R16_G16_B16_A16_SNORM"},
    {0x234, "T_BC4_SNORM"},
    {0x235, "T_BC5_SNORM"},
    {0x301, "TC_R8_SINT"},
    {0x305, "TC_R16_SINT"},
    {0x307, "TC_R8_G8_SINT"},
    {0x30D, "TC_R32_SINT"},
    {0x30F, "TC_R16_G16_SINT"},
    {0x319, "TC_R10_G10_B10_A2_SINT"},
    {0x31A, "TC_R8_G8_B8_A8_SINT"},
    {0x31D, "TC_R32_G32_SINT"},
    {0x31F, "TC_R16_G16_B16_A16_SINT"},
    {0x322, "TC_R32_G32_B32_A32_SINT"},
    {0x41A, "TCS_R8_G8_B8_A8_SRGB"},
    {0x431, "T_BC1_SRGB"},
    {0x432, "T_BC2_SRGB"},
    {0x433, "T_BC3_SRGB"},
    {0x806, "TC_R16_FLOAT"},
    {0x80E, "TCD_R32_FLOAT"},
    {0x810, "TC_R16_G16_FLOAT"},
    {0x811, "D_D24_S8_FLOAT"},
    {0x816, "TC_R11_G11_B10_FLOAT"},
    {0x81C, "T_R32_FLOAT_X8_X24"},
    {0x81E, "TC_R32_G32_FLOAT"},
    {0x820, "TC_R16_G16_B16_A16_FLOAT"},
    {0x823, "TC_R32_G32_B32_A32_FLOAT"},
};

constexpr Name SurfaceDims[] = {
    {0x00, "1D"},
    {0x01, "2D"},
    {0x02, "3D"},
    {0x03, "CUBE"},
    {0x04, "1D_ARRAY"},
    {0x05, "2D_ARRAY"},
    {0x06, "2D_MSAA"},
    {0x07, "2D_MSAA_ARRAY"},
};

constexpr Name TileModes[] = {
    {0x00, "DEFAULT"},
    {0x10, "LINEAR_SPECIAL"},
    {0x20, "DEFAULT_FIX2197"},
    {0x01, "LINEAR_ALIGNED"},
    {0x02, "1D_TILED_THIN1"},
    {0x03, "1D_TILED_THICK"},
    {0x04, "2D_TILED_THIN1"},
    {0x05, "2D_TILED_THIN2"},
    {0x06, "2D_TILED_THIN4"},
    {0x07, "2D_TILED_THICK"},
    {0x08, "2B_TILED_THIN1"},
    {0x09, "2B_TILED_THIN2"},
    {0x0A, "2B_TILED_THIN4"},
    {0x0B, "2B_TILED_THICK"},
    {0x0C, "3D_TILED_THIN1"},
    {0x0D, "3D_TILED_THICK"},
    {0x0E, "3B_TILED_THIN1"},
    {0x0F, "3B_TILED_THICK"},
};

constexpr Name AaModes[] = {
    {0x00, "1X"},
    {0x01, "2X"},
    {0x02, "4X"},
    {0x03, "8X"},
};

template <std::size_t N>
std::string lookup(const Name (&table)[N], u32 value) {
  const auto it = std::find_if(std::begin(table), std::end(table), [&](const Name& n) { return n.value == value; });
  return it != std::end(table) ? std::string(it->name) : "Unknown (" + fmt_hex(value) + ")";
}

}  // namespace

std::string surface_format_name(u32 format) {
  return lookup(SurfaceFormats, format);
}

std::string surface_dim_name(u32 dim) {
  return lookup(SurfaceDims, dim);
}

std::string tile_mode_name(u32 mode) {
  return lookup(TileModes, mode);
}

std::string aa_mode_name(u32 mode) {
  return lookup(AaModes, mode);
}

}  // namespace nnfck::gx2
