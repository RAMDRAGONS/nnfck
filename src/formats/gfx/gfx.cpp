#include "formats/gfx/gfx.hpp"

#include <iterator>

#include "core/report.hpp"

namespace nnfck::gfx {

namespace {

constexpr u32 TypeFormatBits = 8;

constexpr const char* ChannelFormats[] = {
    "Undefined", "R4_G4", "R8", "R4_G4_B4_A4", "A4_B4_G4_R4", "R5_G5_B5_A1", "A1_B5_G5_R5", "R5_G6_B5", "B5_G6_R5", "R8_G8", "R16", "R8_G8_B8_A8", "B8_G8_R8_A8", "R9_G9_B9_E5", "R10_G10_B10_A2", "R11_G11_B10", "B10_G11_R11", "R10_G11_B11", "R16_G16", "R24_G8", "R32", "R16_G16_B16_A16", "R32_G8_X24", "R32_G32", "R32_G32_B32", "R32_G32_B32_A32", "Bc1", "Bc2", "Bc3", "Bc4", "Bc5", "Bc6", "Bc7", "Eac_R11", "Eac_R11_G11", "Etc1", "Etc2", "Etc2_Mask", "Etc2_Alpha", "Pvrtc1_2Bpp", "Pvrtc1_4Bpp", "Pvrtc1_Alpha_2Bpp", "Pvrtc1_Alpha_4Bpp", "Pvrtc2_Alpha_2Bpp", "Pvrtc2_Alpha_4Bpp", "Astc_4x4", "Astc_5x4", "Astc_5x5", "Astc_6x5", "Astc_6x6", "Astc_8x5", "Astc_8x6", "Astc_8x8", "Astc_10x5", "Astc_10x6", "Astc_10x8", "Astc_10x10", "Astc_12x10", "Astc_12x12", "B5_G5_R5_A1"};

constexpr const char* TypeFormats[] = {
    "Undefined", "Unorm", "Snorm", "Uint", "Sint", "Float", "UnormSrgb", "DepthStencil", "UintToFloat", "SintToFloat", "Ufloat"};

constexpr const char* ImageDimensions[] = {
    "1d", "2d", "3d", "CubeMap", "1dArray", "2dArray", "2dMultisample", "2dMultisampleArray", "CubeMapArray"};

constexpr const char* ImageStorageDimensions[] = {
    "Undefined", "1d", "2d", "3d"};

constexpr const char* TileModes[] = {"Optimal", "Linear"};

constexpr const char* NvnFormats[] = {
    "NONE", "R8", "R8SN", "R8UI", "R8I", "R16F", "R16", "R16SN",
    "R16UI", "R16I", "R32F", "R32UI", "R32I", "RG8", "RG8SN", "RG8UI",
    "RG8I", "RG16F", "RG16", "RG16SN", "RG16UI", "RG16I", "RG32F", "RG32UI",
    "RG32I", "RGB8", "RGB8SN", "RGB8UI", "RGB8I", "RGB16F", "RGB16", "RGB16SN",
    "RGB16UI", "RGB16I", "RGB32F", "RGB32UI", "RGB32I", "RGBA8", "RGBA8SN", "RGBA8UI",
    "RGBA8I", "RGBA16F", "RGBA16", "RGBA16SN", "RGBA16UI", "RGBA16I", "RGBA32F", "RGBA32UI",
    "RGBA32I", "STENCIL8", "DEPTH16", "DEPTH24", "DEPTH32F", "DEPTH24_STENCIL8", "DEPTH32F_STENCIL8", "RGBX8_SRGB",
    "RGBA8_SRGB", "RGBA4", "RGB5", "RGB5A1", "RGB565", "RGB10A2", "RGB10A2UI", "R11G11B10F",
    "RGB9E5F", "RGB_DXT1", "RGBA_DXT1", "RGBA_DXT3", "RGBA_DXT5", "RGB_DXT1_SRGB", "RGBA_DXT1_SRGB", "RGBA_DXT3_SRGB",
    "RGBA_DXT5_SRGB", "RGTC1_UNORM", "RGTC1_SNORM", "RGTC2_UNORM", "RGTC2_SNORM", "BPTC_UNORM", "BPTC_UNORM_SRGB", "BPTC_SFLOAT",
    "BPTC_UFLOAT", "R8_UI2F", "R8_I2F", "R16_UI2F", "R16_I2F", "R32_UI2F", "R32_I2F", "RG8_UI2F",
    "RG8_I2F", "RG16_UI2F", "RG16_I2F", "RG32_UI2F", "RG32_I2F", "RGB8_UI2F", "RGB8_I2F", "RGB16_UI2F",
    "RGB16_I2F", "RGB32_UI2F", "RGB32_I2F", "RGBA8_UI2F", "RGBA8_I2F", "RGBA16_UI2F", "RGBA16_I2F", "RGBA32_UI2F",
    "RGBA32_I2F", "RGB10A2SN", "RGB10A2I", "RGB10A2_UI2F", "RGB10A2_I2F", "RGBX8", "RGBX8SN", "RGBX8UI",
    "RGBX8I", "RGBX16F", "RGBX16", "RGBX16SN", "RGBX16UI", "RGBX16I", "RGBX32F", "RGBX32UI",
    "RGBX32I", "RGBA_ASTC_4x4", "RGBA_ASTC_5x4", "RGBA_ASTC_5x5", "RGBA_ASTC_6x5", "RGBA_ASTC_6x6", "RGBA_ASTC_8x5", "RGBA_ASTC_8x6",
    "RGBA_ASTC_8x8", "RGBA_ASTC_10x5", "RGBA_ASTC_10x6", "RGBA_ASTC_10x8", "RGBA_ASTC_10x10", "RGBA_ASTC_12x10", "RGBA_ASTC_12x12", "RGBA_ASTC_4x4_SRGB",
    "RGBA_ASTC_5x4_SRGB", "RGBA_ASTC_5x5_SRGB", "RGBA_ASTC_6x5_SRGB", "RGBA_ASTC_6x6_SRGB", "RGBA_ASTC_8x5_SRGB", "RGBA_ASTC_8x6_SRGB", "RGBA_ASTC_8x8_SRGB", "RGBA_ASTC_10x5_SRGB",
    "RGBA_ASTC_10x6_SRGB", "RGBA_ASTC_10x8_SRGB", "RGBA_ASTC_10x10_SRGB", "RGBA_ASTC_12x10_SRGB", "RGBA_ASTC_12x12_SRGB", "BGR565", "BGR5", "BGR5A1",
    "A1BGR5", "BGRX8", "BGRA8", "BGRX8_SRGB", "BGRA8_SRGB",
};

constexpr const char* NvnTextureTargets[] = {"1D",          "2D",      "3D",           "1D_ARRAY",
                                             "2D_ARRAY",    "2D_MULTISAMPLE", "2D_MULTISAMPLE_ARRAY",
                                             "RECTANGLE",   "CUBEMAP", "CUBEMAP_ARRAY", "BUFFER"};

constexpr const char* LowLevelApis[] = {"Common", "GL", "GX", "D3D", "NVN", "Vulkan", "Ex"};

constexpr const char* ShaderCodeTypes[] = {"Binary", "IR", "Source", "Source Array"};

struct NvnVersion {
  u32 control_version;
  std::string_view platform;
};

constexpr NvnVersion NvnVersions[] = {
    {1, "Switch"},
    {0xC9, "Switch 2"},
};

// Zero, One, Red, Green, Blue, Alpha.
constexpr char ChannelSources[] = {'0', '1', 'R', 'G', 'B', 'A'};

template <std::size_t N>
std::string lookup(const char* const (&table)[N], u32 value) {
  return value < N ? std::string(table[value]) : "Unknown (" + fmt_hex(value) + ")";
}

}  // namespace

std::string image_format_name(u32 format) {
  const u32 channel = format >> TypeFormatBits;
  const u32 type = format & ((1u << TypeFormatBits) - 1);
  if (channel >= std::size(ChannelFormats) || type >= std::size(TypeFormats))
    return "Unknown (" + fmt_hex(format) + ")";
  return std::string(ChannelFormats[channel]) + "_" + TypeFormats[type];
}

std::string image_dimension_name(u32 dimension) {
  return lookup(ImageDimensions, dimension);
}

std::string image_storage_dimension_name(u32 dimension) {
  return lookup(ImageStorageDimensions, dimension);
}

std::string tile_mode_name(u32 mode) {
  return lookup(TileModes, mode);
}

std::string channel_mapping_name(Bytes mapping) {
  std::string out;
  for (const u8 m : mapping)
    out.push_back(m < std::size(ChannelSources) ? ChannelSources[m] : '?');
  return out;
}

std::string low_level_api_name(u32 api) {
  return lookup(LowLevelApis, api);
}

std::string nvn_format_name(u32 format) {
  return lookup(NvnFormats, format);
}

std::string nvn_texture_target_name(u32 target) {
  return lookup(NvnTextureTargets, target);
}

std::string shader_code_type_name(u32 type) {
  return lookup(ShaderCodeTypes, type);
}

std::optional<std::string_view> nvn_platform(u32 control_version) {
  for (const NvnVersion& v : NvnVersions)
    if (v.control_version == control_version)
      return v.platform;
  return std::nullopt;
}

}  // namespace nnfck::gfx
