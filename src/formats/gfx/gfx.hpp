#pragma once

#include <string>

#include "core/bytes.hpp"

// Names for the nn::gfx enumerations that Switch texture resources embed.
namespace nnfck::gfx {

// Image formats pack a channel format above a type format of TypeFormatBits bits.
std::string image_format_name(u32 format);
std::string image_dimension_name(u32 dimension);
std::string image_storage_dimension_name(u32 dimension);
std::string tile_mode_name(u32 mode);
// Four channel sources as a compact string such as "RGBA" or "RRR1".
std::string channel_mapping_name(Bytes mapping);

}  // namespace nnfck::gfx
