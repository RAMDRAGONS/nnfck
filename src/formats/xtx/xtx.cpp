#include "formats/xtx/xtx.hpp"

#include <string>
#include <vector>

#include "formats/gfx/gfx.hpp"
#include "formats/listing.hpp"

namespace nnfck::xtx {

namespace {

using listing::count_of;

constexpr std::size_t HeaderSize = 0x10;
constexpr std::size_t BlockHeaderSize = 0x24;
constexpr std::size_t TextureInfoSize = 0x38;

enum BlockType : s32 {
  TextureInfo = 2,
  ImageData = 3,
  End = 5,
};

// Texture info: image size, alignment, width, height, depth, NVN texture
// target and format, mip count and slice size, then per-mip offsets.
std::string describe_texture(const ByteReader& r, std::size_t at) {
  const u64 image_size = r.u64_at(at);
  const u32 width = r.u32_at(at + 0xC);
  const u32 height = r.u32_at(at + 0x10);
  const u32 depth = r.u32_at(at + 0x14);
  const u32 target = r.u32_at(at + 0x18);
  const u32 format = r.u32_at(at + 0x1C);
  const u32 mips = r.u32_at(at + 0x20);

  std::string size = std::to_string(width) + "x" + std::to_string(height);
  if (depth > 1)
    size += "x" + std::to_string(depth);
  return listing::join({size, gfx::nvn_format_name(format), gfx::nvn_texture_target_name(target),
                        count_of(mips, "mip"), fmt_hex(image_size) + " bytes"});
}

}  // namespace

bool probe(Bytes data) {
  const ByteReader r(data, Endian::Little);
  return r.magic_at(0, "DFvN");
}

// The archive and block signatures are stored as little-endian words, so the
// bytes read "DFvN" and "HBvN".
void analyze(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(Name);
  const ByteReader r(data, Endian::Little);
  r.require(0, HeaderSize, "NvFD archive header");
  const u32 header_size = r.u32_at(0x4);
  if (header_size < HeaderSize || header_size > data.size())
    throw ParseError("NvFD archive header claims " + fmt_hex(header_size) + " bytes");

  Section& s = out.section(std::string(Name));
  s.add("Signature", "NvFD");
  s.add("Version", std::to_string(r.u32_at(0x8)) + "." + std::to_string(r.u32_at(0xC)));

  std::vector<std::string> textures;
  u64 image_bytes = 0;
  std::size_t blocks = 0, images = 0;
  bool ended = false;
  std::size_t p = header_size;
  while (!ended && p < data.size()) {
    r.require(p, BlockHeaderSize, "NvBH block header");
    if (!r.magic_at(p, "HBvN"))
      throw ParseError("block at " + fmt_hex(p) + " lacks its NvBH signature");
    const u32 block_header = r.u32_at(p + 0x4);
    const u64 size = r.u64_at(p + 0x8);
    const u64 offset = r.u64_at(p + 0x10);
    const s32 type = r.s32_at(p + 0x18);
    if (block_header != BlockHeaderSize || offset < BlockHeaderSize || u64{p} + offset > data.size() ||
        size > data.size() - (p + offset))
      throw ParseError("block at " + fmt_hex(p) + " claims " + fmt_hex(size) + " bytes at offset " + fmt_hex(offset));
    const std::size_t at = p + static_cast<std::size_t>(offset);
    ++blocks;
    ctx.spend(BlockHeaderSize, "blocks");

    switch (type) {
    case TextureInfo:
      if (size < TextureInfoSize)
        throw ParseError("texture info at " + fmt_hex(at) + " holds only " + fmt_hex(size) + " bytes");
      if (ctx.list_members) {
        textures.push_back(std::to_string(textures.size()) + ": " + describe_texture(r, at));
        ctx.spend(textures.back().size(), "texture listing");
      } else {
        textures.emplace_back();
      }
      break;
    case ImageData:
      ++images;
      image_bytes += size;
      break;
    case End:
      ended = true;
      break;
    default:
      out.warnings.push_back("NvFD block " + std::to_string(blocks - 1) + " has unknown type " +
                             std::to_string(type));
      break;
    }
    p = at + static_cast<std::size_t>(size);
  }
  if (!ended)
    out.warnings.push_back("NvFD archive has no end block");
  if (images != textures.size())
    out.warnings.push_back("NvFD archive describes " + count_of(textures.size(), "texture") + " but holds " +
                           count_of(images, "image data block"));

  s.add("Blocks", std::to_string(blocks));
  s.add("Textures", std::to_string(textures.size()));
  s.add("Image Data", fmt_size(image_bytes));
  if (!ctx.list_members || textures.empty())
    return;
  Section& list = out.section(std::string(Name) + " Textures");
  for (const std::string& t : textures)
    list.add("", t);
}

}  // namespace nnfck::xtx
