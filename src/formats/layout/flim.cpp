#include "formats/gx2/gx2.hpp"
#include "formats/layout/common.hpp"

// nw::lyt texture images: the GX2 surface comes first so it keeps the file's
// alignment, then the file header and one imag block as a footer. The last
// four bytes repeat the image size, which locates the header.
namespace nnfck::flim {

namespace {

using namespace layout;

constexpr std::size_t ImageBlockSize = 0x10;
constexpr std::size_t FooterSize = FileHeaderSize + ImageBlockSize + 4;

constexpr std::string_view FormatNames[] = {
    "L8",        "A8",         "LA4",        "LA8",        "HILO8",    "RGB565",          "RGB8",
    "RGB5A1",    "RGBA4",      "RGBA8",      "ETC1",       "ETC1A4",   "BC1",             "BC2",
    "BC3",       "BC4 (L)",    "BC4 (A)",    "BC5",        "L4",       "A4",              "RGBA8 sRGB",
    "BC1 sRGB",  "BC2 sRGB",   "BC3 sRGB",   "RGB10A2",    "RGB565 (indirect)",
};

std::string format_name(u8 format) {
  if (format < std::size(FormatNames))
    return std::string(FormatNames[format]);
  return "Unknown (" + std::to_string(format) + ")";
}

// The header's offset when `data` ends in an image size that points at it.
std::size_t header_at(Bytes data, Endian e) {
  if (data.size() < FooterSize)
    return data.size();
  const ByteReader r(data, e);
  const u64 at = (u64{r.u32_at(data.size() - 4)} + 3) & ~u64{3};
  if (at > data.size() - FooterSize || !r.magic_at(static_cast<std::size_t>(at), "FLIM"))
    return data.size();
  return static_cast<std::size_t>(at);
}

}  // namespace

bool probe(Bytes data) {
  for (const Endian e : {Endian::Big, Endian::Little}) {
    const std::size_t at = header_at(data, e);
    if (at < data.size() && layout::probe(data.subspan(at), "FLIM"))
      return true;
  }
  return false;
}

void analyze(Bytes data, Analysis& out, const Context&) {
  std::size_t at = header_at(data, Endian::Big);
  if (at == data.size())
    at = header_at(data, Endian::Little);
  if (at == data.size())
    throw ParseError("no FLIM header at the image size the file ends with");

  ByteReader r(data.subspan(at), Endian::Big);
  Section& s = out.section(std::string(Name));
  const FileHeader h = read_header(r, Name, out, s, data.size());
  const u32 image_size = ByteReader(data, r.endian()).u32_at(data.size() - 4);
  s.add("Header Offset", fmt_hex(at));
  s.add("Image Size", fmt_size(image_size));

  const std::size_t b = h.header_size;
  r.require(b, ImageBlockSize, "image block");
  if (!r.magic_at(b, "imag"))
    throw ParseError("FLIM block at " + fmt_hex(at + b) + " is " + fmt_quoted(r.data().subspan(b, 4)) +
                     " instead of \"imag\"");
  const u8 tiling = r.u8_at(b + 0xF);
  s.add("Width", std::to_string(r.u16_at(b + 8)));
  s.add("Height", std::to_string(r.u16_at(b + 0xA)));
  s.add("Alignment", fmt_hex(r.u16_at(b + 0xC)));
  s.add("Format", format_name(r.u8_at(b + 0xE)));
  s.add("Tile Mode", gx2::tile_mode_name(tiling & 0x1F));
  s.add("Swizzle", std::to_string(tiling >> 5));

  if (h.block_count != 1)
    out.warnings.push_back("FLIM declares " + std::to_string(h.block_count) + " blocks instead of one");
  if (r.u32_at(b + 4) != ImageBlockSize)
    out.warnings.push_back("FLIM image block declares size " + fmt_hex(r.u32_at(b + 4)) + " instead of " +
                           fmt_hex(ImageBlockSize));
}

}  // namespace nnfck::flim
