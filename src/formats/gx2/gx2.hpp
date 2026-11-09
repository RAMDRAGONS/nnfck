#pragma once

#include <string>

#include "core/bytes.hpp"

// Names for the GX2 surface enumerations that Wii U texture resources embed.
namespace nnfck::gx2 {

std::string surface_format_name(u32 format);
std::string surface_dim_name(u32 dim);
std::string tile_mode_name(u32 mode);
std::string aa_mode_name(u32 mode);

}  // namespace nnfck::gx2
