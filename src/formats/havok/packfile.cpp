#include "formats/havok/packfile.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace nnfck::havok {

namespace {

constexpr u32 Magic0 = 0x57E0E057;
constexpr u32 Magic1 = 0x10C0C010;
constexpr std::size_t HeaderSize = 0x40;
constexpr std::size_t SectionTagLength = 19;
constexpr std::size_t VersionStringLength = 16;
constexpr std::size_t MaxClassNameLength = 0x100;
// Version 11 headers gained a predicate array and padded section headers.
constexpr s32 PredicateVersion = 11;

struct SectionHeader {
  std::string tag;
  s32 data_start;
  s32 end;
};

std::string fixed_string(Bytes bytes) {
  std::string s;
  for (const u8 c : bytes) {
    if (c == 0)
      break;
    s.push_back(static_cast<char>(c));
  }
  return s;
}

// Parses the packfile at the start of `data` and returns the offset where its
// last section ends, relative to that start.
u64 analyze_one(Bytes data, Analysis& out, std::string title) {
  ByteReader r(data, Endian::Big);
  r.require(0, HeaderSize, "Havok packfile header");

  const u8 pointer_size = r.u8_at(0x10);
  const u8 little_endian = r.u8_at(0x11);
  const u8 reuse_padding = r.u8_at(0x12);
  const u8 empty_base = r.u8_at(0x13);
  r.set_endian(little_endian ? Endian::Little : Endian::Big);

  const s32 user_tag = r.s32_at(0x08);
  const s32 file_version = r.s32_at(0x0C);
  const s32 num_sections = r.s32_at(0x14);
  const s32 contents_section = r.s32_at(0x18);
  const s32 contents_offset = r.s32_at(0x1C);
  const s32 class_name_section = r.s32_at(0x20);
  const s32 class_name_offset = r.s32_at(0x24);
  const std::string contents_version = fixed_string(data.subspan(0x28, VersionStringLength));
  const s32 flags = r.s32_at(0x38);

  Section& s = out.section(std::move(title));
  s.add("File Version", std::to_string(file_version));
  s.add("Contents Version", contents_version);
  s.add("User Tag", fmt_hex(static_cast<u32>(user_tag)));
  s.add("Layout", "");
  s.add("Pointer Size", std::to_string(pointer_size) + " bytes", 1);
  s.add("Byte Order", std::string(endian_name(r.endian())), 1);
  s.add("Reuse Padding", fmt_bool(reuse_padding != 0), 1);
  s.add("Empty Base Class", fmt_bool(empty_base != 0), 1);
  s.add("Flags", fmt_hex(static_cast<u32>(flags)));

  std::size_t section_table = HeaderSize;
  std::size_t section_header_size = 0x30;
  if (file_version >= PredicateVersion) {
    const s16 max_predicate = r.s16_at(0x3C);
    const s16 predicate_bytes = r.s16_at(0x3E);
    if (max_predicate != -1) {
      if (predicate_bytes < 0)
        throw ParseError("negative predicate array size " + std::to_string(predicate_bytes));
      section_table += static_cast<std::size_t>(predicate_bytes);
    }
    section_header_size = 0x40;
  }

  if (num_sections < 0 || num_sections > 64)
    throw ParseError("implausible section count " + std::to_string(num_sections));
  r.require(section_table, static_cast<std::size_t>(num_sections) * section_header_size, "section headers");

  std::vector<SectionHeader> sections;
  s.add("Sections", std::to_string(num_sections));
  for (s32 i = 0; i < num_sections; ++i) {
    const std::size_t h = section_table + static_cast<std::size_t>(i) * section_header_size;
    SectionHeader sh{fixed_string(data.subspan(h, SectionTagLength)), r.s32_at(h + 0x14), r.s32_at(h + 0x2C)};
    if (sh.data_start < 0 || sh.end < 0 || u64(sh.data_start) + u64(sh.end) > data.size())
      throw ParseError("section " + std::to_string(i) + " (" + sh.tag + ") lies outside the file");
    s.add(sh.tag.empty() ? "(unnamed)" : sh.tag,
          fmt_hex(static_cast<u32>(sh.data_start)) + ", " + fmt_size(static_cast<u32>(sh.end)), 1);
    sections.push_back(std::move(sh));
  }

  const auto in_range = [&](s32 idx) { return idx >= 0 && idx < num_sections; };
  if (in_range(class_name_section) && in_range(contents_section)) {
    const SectionHeader& names = sections[static_cast<std::size_t>(class_name_section)];
    const u64 at = u64(names.data_start) + static_cast<u32>(class_name_offset);
    if (class_name_offset >= 0 && class_name_offset < names.end && at < data.size())
      s.add("Contents Class", r.cstring_at(static_cast<std::size_t>(at), MaxClassNameLength));
    s.add("Contents Location",
          sections[static_cast<std::size_t>(contents_section)].tag + " + " +
              fmt_hex(static_cast<u32>(contents_offset)));
  } else {
    out.warnings.push_back("Havok contents section indices are out of range");
  }

  u64 end = section_table + static_cast<u64>(num_sections) * section_header_size;
  for (const SectionHeader& sh : sections)
    end = std::max(end, u64(sh.data_start) + u64(sh.end));
  return end;
}

}  // namespace

bool probe(Bytes data) {
  // Both magic words are byte palindromes, so they read the same in either order.
  const ByteReader r(data, Endian::Big);
  return r.has(0, 8) && r.u32_at(0) == Magic0 && r.u32_at(4) == Magic1;
}

void analyze(Bytes data, Analysis& out, const Context&) {
  // Some files chain several packfiles back to back, each starting where the
  // previous one's sections end.
  std::size_t offset = 0;
  std::size_t count = 0;
  while (true) {
    const Bytes rest = data.subspan(offset);
    std::string title(Name);
    if (count != 0)
      title += " #" + std::to_string(count + 1) + " at " + fmt_hex(offset);
    const u64 end = analyze_one(rest, out, std::move(title));
    ++count;
    offset += static_cast<std::size_t>(end);
    if (offset >= data.size() || !probe(data.subspan(offset)))
      break;
  }

  out.format = std::string(Name);
  if (count > 1)
    out.format += " (" + std::to_string(count) + " concatenated)";
  if (offset != data.size())
    out.warnings.push_back("Havok data ends at " + fmt_hex(offset) + " but the file is " + fmt_hex(data.size()) +
                           " bytes");
}

}  // namespace nnfck::havok
