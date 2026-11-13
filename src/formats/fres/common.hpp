#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "formats/byte_order.hpp"
#include "formats/fres/fres.hpp"
#include "formats/listing.hpp"

namespace nnfck::fres {

inline constexpr std::size_t MaxNameLength = 0x400;
inline constexpr std::size_t DictNodeSize = 0x10;

inline const std::string Title(Name);

struct ListEntry {
  std::string name;
  // Offset of the entry's block, when the dictionary records one.
  u64 data = 0;
  // One-line summary of the block, empty when not parsed.
  std::string detail;
};

struct Group {
  std::string label;
  std::vector<ListEntry> entries;
};

// Prints each non-empty group as a count followed by one row per entry.
void add_groups(Analysis& out, const std::vector<Group>& groups);

void add_external_files(Analysis& out, const std::vector<ListEntry>& names,
                        const std::vector<std::pair<u64, u64>>& spans, Bytes data, const Context& ctx);

void check_file_size(Analysis& out, u32 declared, std::size_t actual);
std::size_t max_dict_entries(const ByteReader& r, std::size_t dict);

using listing::count_of;
using listing::join;
// Appends "looping" and "baked" for the animation flag bits shared by every
// animation type on both platforms.
void add_anim_flags(std::vector<std::string>& parts, u32 flag);

std::string frames(s32 n);
// A count that disagrees with the dictionary or index group (`source`)
// listing the same elements.
void expect_count(Analysis& out, const std::string& owner, std::string_view what, u32 declared, std::size_t listed,
                  std::string_view source);

// Wii U blocks refer to each other through self-relative 32-bit offsets.
u64 cafe_offset(const ByteReader& r, std::size_t field);
std::string cafe_string(const ByteReader& r, std::size_t field);
std::size_t cafe_dict_count(const ByteReader& r, std::size_t field);

// Switch blocks refer to each other through absolute 64-bit offsets.
std::string nx_string(const ByteReader& r, u64 at);
std::size_t nx_dict_count(const ByteReader& r, std::size_t field);

// Field offsets within each Switch block type. Before 9.x every block opened
// with a 16-byte block header and kept its flags near the end; from 9.x a
// 4-byte signature is followed directly by the flags and the counts were
// reordered.
struct NxLayout {
  std::size_t name;  // every named block keeps its name pointer here

  std::size_t model_stride, model_skeleton, model_vertices, model_shape_dic, model_material_dic;
  std::size_t model_vertex_count, model_shape_count, model_material_count;
  std::size_t skeleton_bone_count;
  std::size_t vertex_stride, vertex_count;

  std::size_t skeletal_stride, skeletal_flag, skeletal_frames, skeletal_curves, skeletal_bone_anims;

  std::size_t material_stride, material_flag, material_frames, material_curves, material_per_material;
  std::size_t material_shader_param, material_tex_pattern, material_visibility, material_textures;

  std::size_t bone_vis_stride, bone_vis_flag, bone_vis_frames, bone_vis_anims, bone_vis_curves;

  std::size_t shape_stride, shape_flag, shape_frames, shape_vertex_anims, shape_key_anims, shape_curves;

  std::size_t scene_stride, scene_cameras, scene_lights, scene_fogs;
};

void analyze_cafe(ByteReader& r, Analysis& out, const Context& ctx);
void analyze_nx(ByteReader& r, Analysis& out, const Context& ctx);

}  // namespace nnfck::fres
