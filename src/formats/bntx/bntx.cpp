#include "formats/bntx/bntx.hpp"

#include <string>
#include <vector>

#include "formats/gfx/gfx.hpp"

// nn::gfx texture resource files: an nn::util::BinaryFileHeader, a texture
// container, and one BRTI block per texture.
namespace nnfck::bntx {

namespace {

constexpr std::size_t HeaderSize = 0x20;
constexpr std::size_t ContainerSize = 0x38;
constexpr std::size_t TextureSize = 0xA0;
constexpr std::size_t DictNodeSize = 0x10;

std::string bntx_string(const ByteReader& r, u64 at) {
  if (at == 0)
    return {};
  const u16 length = r.u16_at(static_cast<std::size_t>(at));
  r.require(static_cast<std::size_t>(at) + 2, length, "BNTX string");
  const Bytes s = r.data().subspan(static_cast<std::size_t>(at) + 2, length);
  return std::string(s.begin(), s.end());
}

std::string count_of(u64 n, std::string_view singular) {
  return std::to_string(n) + " " + std::string(singular) + (n == 1 ? "" : "s");
}

// BRTI: a 16-byte block header, the texture info, then data size, alignment,
// channel mapping, dimension and the name pointer.
std::string describe(const ByteReader& r, std::size_t at) {
  const u8 storage = r.u8_at(at + 0x11);
  const u16 tile = r.u16_at(at + 0x12);
  const u16 mips = r.u16_at(at + 0x16);
  const u16 samples = r.u16_at(at + 0x18);
  const u32 format = r.u32_at(at + 0x1C);
  const u32 width = r.u32_at(at + 0x24);
  const u32 height = r.u32_at(at + 0x28);
  const u32 depth = r.u32_at(at + 0x2C);
  const u32 array_length = r.u32_at(at + 0x30);
  const u32 data_size = r.u32_at(at + 0x50);
  const u8 dimension = r.u8_at(at + 0x5C);

  std::string size = std::to_string(width) + "x" + std::to_string(height);
  if (storage == 3 && depth > 1)
    size += "x" + std::to_string(depth);
  std::vector<std::string> parts{size, gfx::image_format_name(format), count_of(mips, "mip"),
                                 gfx::image_dimension_name(dimension)};
  if (array_length > 1)
    parts.push_back(count_of(array_length, "layer"));
  if (samples > 1)
    parts.push_back(count_of(samples, "sample"));
  parts.push_back(gfx::tile_mode_name(tile));
  parts.push_back("channels " + gfx::channel_mapping_name(r.data().subspan(at + 0x58, 4)));
  parts.push_back(fmt_hex(data_size) + " bytes");

  std::string out;
  for (const std::string& p : parts)
    out += (out.empty() ? "" : ", ") + p;
  return out;
}

}  // namespace

bool probe(Bytes data) {
  const ByteReader r(data, Endian::Big);
  return r.magic_at(0, std::string_view("BNTX\0\0\0\0", 8));
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(Name);
  ByteReader r(data, Endian::Little);
  r.require(0, HeaderSize + ContainerSize, "BNTX header");
  const u8 b0 = r.u8_at(0xC), b1 = r.u8_at(0xD);
  if (b0 == 0xFE && b1 == 0xFF)
    r.set_endian(Endian::Big);
  else if (!(b0 == 0xFF && b1 == 0xFE))
    throw ParseError("invalid byte order mark " + fmt_hex_bytes(data.subspan(0xC, 2)));

  const u32 version = r.u32_at(0x08);
  const u8 alignment_shift = r.u8_at(0x0E);
  const u8 address_size = r.u8_at(0x0F);
  const u32 name_offset = r.u32_at(0x10);
  const u16 flags = r.u16_at(0x14);
  const u16 first_block = r.u16_at(0x16);
  const u32 relocation_table = r.u32_at(0x18);
  const u32 file_size = r.u32_at(0x1C);

  const Bytes platform = data.subspan(0x20, 4);
  const u32 texture_count = r.u32_at(0x24);
  const u64 texture_ptrs = r.u64_at(0x28);
  const u64 texture_dic = r.u64_at(0x38);

  Section& s = out.section(std::string(Name));
  s.add("Version", std::to_string(version >> 16) + "." + std::to_string((version >> 8) & 0xFF) + "." +
                       std::to_string(version & 0xFF));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Alignment", alignment_shift < 32 ? fmt_hex(u64{1} << alignment_shift)
                                          : "Invalid (2^" + std::to_string(alignment_shift) + ")");
  s.add("Target Address Size", std::to_string(address_size));
  s.add("Flags", fmt_hex(flags, 4));
  s.add("First Block Offset", fmt_hex(first_block));
  s.add("Relocation Table", relocation_table == 0 ? "None" : fmt_hex(relocation_table));
  s.add("File Size", fmt_size(file_size));
  if (name_offset != 0)
    s.add("Name", r.cstring_at(name_offset, 0x400));
  s.add("Target Platform", fmt_quoted(platform));
  s.add("Textures", std::to_string(texture_count));

  if (file_size != data.size())
    out.warnings.push_back("BNTX declares " + fmt_hex(file_size) + " bytes but holds " + fmt_hex(data.size()));
  if (relocation_table != 0 && !r.magic_at(relocation_table, "_RLT"))
    out.warnings.push_back("BNTX relocation table at " + fmt_hex(relocation_table) + " lacks its _RLT signature");

  // The dictionary names textures in the same order as the pointer array.
  std::vector<std::string> names;
  if (texture_dic != 0) {
    const std::size_t dic = static_cast<std::size_t>(texture_dic);
    const s32 count = r.s32_at(dic + 4);
    if (count < 0 || static_cast<u32>(count) != texture_count)
      out.warnings.push_back("BNTX dictionary names " + std::to_string(count) + " textures but the container holds " +
                             std::to_string(texture_count));
    for (s32 i = 1; i <= count && static_cast<u32>(i) <= texture_count; ++i) {
      names.push_back(bntx_string(r, r.u64_at(dic + 8 + static_cast<std::size_t>(i) * DictNodeSize + 8)));
      ctx.spend(DictNodeSize + names.back().size(), "dictionary names");
    }
  }

  if (texture_count != 0)
    r.require(static_cast<std::size_t>(texture_ptrs), std::size_t(texture_count) * 8, "BNTX texture pointer array");
  std::vector<std::string> rows;
  for (u32 i = 0; i < texture_count; ++i) {
    const u64 at = r.u64_at(static_cast<std::size_t>(texture_ptrs) + std::size_t(i) * 8);
    if (!r.has(static_cast<std::size_t>(at), TextureSize) || !r.magic_at(static_cast<std::size_t>(at), "BRTI")) {
      out.errors.push_back("BNTX texture " + std::to_string(i) + " at " + fmt_hex(at) + " is not a BRTI block");
      continue;
    }
    const std::size_t t = static_cast<std::size_t>(at);
    const std::string name = bntx_string(r, r.u64_at(t + 0x60));
    // Many pointers may name the same texture block.
    ctx.spend(TextureSize + name.size(), "texture blocks");
    if (i < names.size() && names[i] != name)
      out.warnings.push_back("BNTX texture named \"" + name + "\" is listed as \"" + names[i] + "\"");
    if (ctx.list_members)
      rows.push_back(name + ": " + describe(r, t));
  }

  if (rows.empty())
    return;
  Section& list = out.section(std::string(Name) + " Textures");
  for (const std::string& row : rows)
    list.add("", row);
}

}  // namespace nnfck::bntx
