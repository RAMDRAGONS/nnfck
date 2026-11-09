#include "formats/layout/common.hpp"

namespace nnfck::layout {

bool probe(Bytes data, std::string_view magic) {
  const ByteReader r(data, Endian::Big);
  if (!r.has(0, FileHeaderSize) || !r.magic_at(0, magic))
    return false;
  const u16 bom = r.u16_at(4);
  return bom == 0xFEFF || bom == 0xFFFE;
}

std::string version_string(u32 version) {
  return std::to_string(version >> 24) + "." + std::to_string((version >> 16) & 0xFF) + "." +
         std::to_string((version >> 8) & 0xFF) + "." + std::to_string(version & 0xFF);
}

// Wii U builds are big-endian and Switch builds little-endian; the format
// itself is the same on both.
FileHeader read_header(ByteReader& r, std::string_view name, Analysis& out, Section& s, std::size_t file_size) {
  r.require(0, FileHeaderSize, std::string(name) + " header");
  r.set_endian(r.u16_at(4) == 0xFEFF ? Endian::Big : Endian::Little);
  const FileHeader h{r.u16_at(6), r.u32_at(8), r.u32_at(0xC), r.u16_at(0x10)};
  const std::string_view platform = r.endian() == Endian::Big ? "Wii U" : "Switch";
  out.format = std::string(name) + " (" + std::string(platform) + ")";

  s.add("Platform", std::string(platform));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Version", version_string(h.version));
  s.add("Header Size", fmt_hex(h.header_size));
  s.add("File Size", fmt_size(h.file_size));
  s.add("Blocks", std::to_string(h.block_count));

  if (h.file_size != file_size)
    out.warnings.push_back(std::string(name) + " declares " + fmt_hex(h.file_size) + " bytes but holds " +
                           fmt_hex(file_size));
  if (h.header_size < FileHeaderSize || h.header_size > r.size())
    throw ParseError("header size " + fmt_hex(h.header_size) + " is outside the file");
  return h;
}

std::vector<Block> read_blocks(const ByteReader& r, const FileHeader& h, std::string_view what, Analysis& out,
                               const Context& ctx) {
  std::vector<Block> blocks;
  std::size_t at = h.header_size;
  for (u16 i = 0; i < h.block_count; ++i) {
    if (!r.has(at, BlockHeaderSize)) {
      out.errors.push_back(std::string(what) + " block " + std::to_string(i) + " at " + fmt_hex(at) +
                           " is outside the file");
      return blocks;
    }
    const u32 size = r.u32_at(at + 4);
    if (size < BlockHeaderSize || size > r.size() - at) {
      out.errors.push_back(std::string(what) + " block " + std::to_string(i) + " at " + fmt_hex(at) +
                           " declares size " + fmt_hex(size) + ", outside the file");
      return blocks;
    }
    ctx.spend(BlockHeaderSize, "blocks");
    const Bytes kind = r.data().subspan(at, 4);
    blocks.push_back({std::string(kind.begin(), kind.end()), at, size});
    at += size;
  }
  if (at != r.size())
    out.warnings.push_back(std::string(what) + " blocks end at " + fmt_hex(at) + " of " + fmt_hex(r.size()) +
                           " bytes");
  return blocks;
}

std::string fixed_name(const ByteReader& r, std::size_t at, std::size_t width) {
  r.require(at, width, "name");
  std::string s;
  for (std::size_t i = 0; i < width && r.u8_at(at + i) != 0; ++i)
    s.push_back(static_cast<char>(r.u8_at(at + i)));
  return s;
}

std::vector<std::string> offset_names(const ByteReader& r, std::size_t table, std::size_t count, std::size_t end,
                                      std::string_view what, const Context& ctx) {
  if (count > (end - std::min(table, end)) / 4)
    throw ParseError(std::to_string(count) + " " + std::string(what) + " do not fit their block");
  std::vector<std::string> names;
  names.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const u64 at = u64{table} + r.u32_at(table + i * 4);
    if (at >= end)
      throw ParseError(std::string(what) + " name " + std::to_string(i) + " lies outside its block");
    names.push_back(r.cstring_at(static_cast<std::size_t>(at), std::min(MaxNameLength, end - at)));
    ctx.spend(4 + names.back().size(), what);
  }
  return names;
}


namespace {

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

}  // namespace

std::string text_body_kind(Bytes data) {
  const std::string root = xml::root_name(data);
  if (root.substr(root.find(':') + 1) != TextRoot)
    return {};
  const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
  std::size_t at = 0;
  // The body may carry a namespace prefix like any other element.
  for (;;) {
    at = text.find("body", at);
    if (at == std::string_view::npos)
      return {};
    const char before = at > 0 ? text[at - 1] : '\0';
    const char after = at + 4 < text.size() ? text[at + 4] : '\0';
    const bool opens = (before == '<' || before == ':') && (after == '>' || is_space(after));
    at += 4;
    if (opens)
      break;
  }
  at = text.find('>', at);
  while (at != std::string_view::npos) {
    at = text.find('<', at);
    if (at == std::string_view::npos)
      return {};
    if (text.substr(at).starts_with("<!--")) {
      at = text.find("-->", at);
      continue;
    }
    if (text.substr(at).starts_with("<?")) {
      at = text.find("?>", at);
      continue;
    }
    std::size_t end = at + 1;
    while (end < text.size() && text[end] != '>' && text[end] != '/' && !is_space(text[end]))
      ++end;
    const std::string_view name = text.substr(at + 1, end - at - 1);
    return std::string(name.substr(name.find(':') == std::string_view::npos ? 0 : name.find(':') + 1));
  }
  return {};
}

xml::Element parse_text(Bytes data, const Context& ctx) {
  ctx.spend(data.size(), "XML");
  return xml::parse(data);
}

void add_text_header(const xml::Element& root, Section& s) {
  if (const std::string* v = root.attribute("version"))
    s.add("Version", *v);
  const xml::Element* head = root.child("head");
  if (head == nullptr)
    return;
  if (const xml::Element* g = head->child("generator")) {
    const std::string name = g->attribute_or("name");
    const std::string version = g->attribute_or("version");
    if (!name.empty() || !version.empty())
      s.add("Generator", name.empty() ? version : version.empty() ? name : name + " " + version);
  }
  if (const xml::Element* c = head->child("create")) {
    if (const std::string date = c->attribute_or("date"); !date.empty())
      s.add("Created", date);
    if (const std::string user = c->attribute_or("user"); !user.empty())
      s.add("Created By", user);
  }
}

const xml::Element* text_body(const xml::Element& root) {
  return root.local_name() == TextRoot ? root.child("body") : nullptr;
}

}  // namespace nnfck::layout
