#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/bytes.hpp"

// Names for the nn::gfx enumerations that Switch texture and shader resources
// embed.
namespace nnfck::gfx {

// Image formats pack a channel format above a type format of TypeFormatBits bits.
std::string image_format_name(u32 format);
std::string image_dimension_name(u32 dimension);
std::string image_storage_dimension_name(u32 dimension);
std::string tile_mode_name(u32 mode);
// Four channel sources as a compact string such as "RGBA" or "RRR1".
std::string channel_mapping_name(Bytes mapping);

std::string low_level_api_name(u32 api);
// NVN's own texture enumerations, as stored by its texture packager.
std::string nvn_format_name(u32 format);
std::string nvn_texture_target_name(u32 target);
std::string shader_code_type_name(u32 type);

// NVN shader control sections open with this magic and a version that tells
// the GPU generation apart; shader containers repeat that version in their
// low-level compiler version.
inline constexpr u32 NvnControlMagic = 0x98761234;
std::optional<std::string_view> nvn_platform(u32 control_version);

}  // namespace nnfck::gfx
