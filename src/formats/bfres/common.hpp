#pragma once

#include <string>
#include <utility>
#include <vector>

#include "formats/bfres/bfres.hpp"

namespace nnfck::bfres {

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

Endian read_bom(const ByteReader& r, std::size_t offset);
void check_file_size(Analysis& out, u32 declared, std::size_t actual);
std::size_t max_dict_entries(const ByteReader& r, std::size_t dict);

std::string count_of(u64 n, std::string_view singular, std::string_view plural = {});
std::string join(const std::vector<std::string>& parts);
// Appends "looping" and "baked" for the animation flag bits shared by every
// animation type on both platforms.
void add_anim_flags(std::vector<std::string>& parts, u32 flag);

void analyze_cafe(ByteReader& r, Analysis& out, const Context& ctx);
void analyze_nx(ByteReader& r, Analysis& out, const Context& ctx);

}  // namespace nnfck::bfres
