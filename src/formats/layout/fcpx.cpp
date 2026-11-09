#include "formats/layout/common.hpp"

// nn::font complex fonts: after the file header, one offset to a tree of
// sub-fonts. Pair fonts join two sub-fonts; the leaves are bitmap fonts or
// sets of scalable font faces, each naming the font file it loads.
namespace nnfck::fcpx {

namespace {

using namespace layout;

constexpr u32 BitmapFont = 0;
constexpr u32 MultiScalableFont = 1;
constexpr u32 PairFont = 2;
constexpr std::size_t BitmapFontSize = 0x10;
constexpr std::size_t PairFontSize = 0xC;
constexpr std::size_t MultiScalableFontSize = 0x20;
constexpr std::size_t ScalableFaceSize = 0x28;
// Resources up to 8.0 predate the line feed offset, alternate character and
// per-face metrics overrides.
constexpr u32 LegacyVersion = 0x08000000;
constexpr std::size_t LegacyMultiScalableFontSize = 0x10;
constexpr std::size_t LegacyScalableFaceSize = 0x18;
constexpr std::size_t CharRangeSize = 8;
constexpr int MaxTreeDepth = 16;

struct Walker {
  const ByteReader& r;
  bool legacy;
  Group& rows;
  u32 files = 0;
  Analysis& out;
  const Context& ctx;

  std::string name_at(std::size_t base, u32 offset) const {
    const u64 at = u64{base} + offset;
    if (at >= r.size())
      throw ParseError("font name at " + fmt_hex(at) + " is outside the file");
    return r.cstring_at(static_cast<std::size_t>(at), MaxNameLength);
  }

  // A count and an offset, relative to `base`, to pairs of first and last
  // character codes.
  std::string ranges(std::size_t base, std::size_t set) const {
    const s32 count = r.s32_at(set);
    if (count <= 0)
      return "all characters";
    const u64 at = u64{base} + r.u32_at(set + 4);
    if (at > r.size() || u64(count) * CharRangeSize > r.size() - at)
      throw ParseError(std::to_string(count) + " character ranges at " + fmt_hex(at) + " pass the end of the file");
    ctx.spend(u64(count) * CharRangeSize, "character ranges");
    std::vector<std::string> parts;
    for (s32 i = 0; i < count; ++i) {
      const std::size_t e = static_cast<std::size_t>(at) + std::size_t(i) * CharRangeSize;
      parts.push_back(fmt_hex(r.u32_at(e), 4) + " to " + fmt_hex(r.u32_at(e + 4), 4));
    }
    return join(parts);
  }

  void node(u64 at, int depth) {
    if (depth > MaxTreeDepth) {
      out.errors.push_back("Complex font tree nests deeper than " + std::to_string(MaxTreeDepth) + " levels");
      return;
    }
    if (at + 4 > r.size()) {
      out.errors.push_back("Complex font node at " + fmt_hex(at) + " is outside the file");
      return;
    }
    const std::size_t n = static_cast<std::size_t>(at);
    ctx.spend(BitmapFontSize, "font tree");
    switch (const u32 type = r.u32_at(n); type) {
    case BitmapFont:
      r.require(n, BitmapFontSize, "bitmap font");
      ++files;
      rows.rows.push_back({"Bitmap font " + name_at(n, r.u32_at(n + 4)) + ": " + ranges(n, n + 8), depth});
      break;
    case PairFont:
      r.require(n, PairFontSize, "pair font");
      rows.rows.push_back({"Pair font", depth});
      node(at + r.u32_at(n + 4), depth + 1);
      node(at + r.u32_at(n + 8), depth + 1);
      break;
    case MultiScalableFont:
      scalable(n, depth);
      break;
    default:
      out.errors.push_back("Complex font node at " + fmt_hex(at) + " has unknown type " + std::to_string(type));
    }
  }

  void scalable(std::size_t n, int depth) {
    r.require(n, legacy ? LegacyMultiScalableFontSize : MultiScalableFontSize, "scalable font");
    const float size = r.f32_at(n + 4);
    const u32 count = r.u32_at(n + (legacy ? 0x8 : 0xC));
    const u64 faces = u64{n} + r.u32_at(n + (legacy ? 0xC : 0x10));
    const std::size_t stride = legacy ? LegacyScalableFaceSize : ScalableFaceSize;
    if (faces > r.size() || u64{count} * stride > r.size() - faces) {
      out.errors.push_back("Complex font lists " + std::to_string(count) + " scalable faces past the end of the file");
      return;
    }
    std::string row = "Scalable font, size " + fmt_float(size);
    if (!legacy && r.u16_at(n + 0x14) != 0)
      row += ", alternate character " + fmt_hex(r.u16_at(n + 0x14), 4);
    rows.rows.push_back({std::move(row), depth});
    for (u32 i = 0; i < count; ++i) {
      const std::size_t f = static_cast<std::size_t>(faces) + std::size_t{i} * stride;
      ctx.spend(stride, "scalable faces");
      // Face names and character ranges are relative to the scalable font,
      // not to the face.
      const u32 name = r.u32_at(f + (legacy ? 0x8 : 0xC));
      const u32 font_index = r.u32_at(f + (legacy ? 0x4 : 0x8));
      ++files;
      std::string face = name_at(n, name);
      if (font_index != 0)
        face += " #" + std::to_string(font_index);
      rows.rows.push_back({face + ": " + ranges(n, f + (legacy ? 0x10 : 0x20)), depth + 1});
    }
  }
};

}  // namespace

bool probe(Bytes data) {
  return layout::probe(data, "FCPX");
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  ByteReader r(data, Endian::Big);
  Section& s = out.section(std::string(Name));
  const FileHeader h = read_header(r, Name, out, s, data.size());
  const bool legacy = h.version <= LegacyVersion;
  r.require(h.header_size, 4, "complex font root");

  Group fonts{"Font Tree"};
  Walker w{r, legacy, fonts, 0, out, ctx};
  w.node(u64{h.header_size} + r.u32_at(h.header_size), 0);
  fonts.summary = count_of(w.files, "font file");
  s.add("Font Files", std::to_string(w.files));
  add_groups(out, std::string(Name) + " Contents", {fonts});
}

namespace {

constexpr std::string_view TextRootName = "complexFontDescription";

// Character codes are written in decimal.
std::string text_ranges(const xml::Element& font) {
  const xml::Element* set = font.child("charCodeRangeSet");
  const auto ranges = set ? set->children_named("charCodeRange") : std::vector<const xml::Element*>{};
  if (ranges.empty())
    return "all characters";
  std::vector<std::string> parts;
  for (const xml::Element* r : ranges) {
    const auto code = [&](std::string_view key) {
      const std::string v = r->attribute_or(key);
      u64 n = 0;
      for (const char c : v) {
        if (c < '0' || c > '9' || n > 0xFFFFFFFF)
          return v;
        n = n * 10 + static_cast<u64>(c - '0');
      }
      return v.empty() || n > 0xFFFFFFFF ? v : fmt_hex(n, 4);
    };
    std::string part = code("first") + " to " + code("last");
    if (r->attribute_or("exclusion") == "true")
      part = "not " + part;
    parts.push_back(std::move(part));
  }
  return join(parts);
}

std::string text_path(const xml::Element& font) {
  const xml::Element* path = font.child("path");
  return path ? path->text : std::string();
}

struct TextWalker {
  Group& rows;
  u32 files = 0;
  Analysis& out;
  const Context& ctx;

  void face(const xml::Element& d, int depth) {
    ++files;
    std::string name = text_path(d);
    if (const std::string index = d.attribute_or("ttcFontIndex"); !index.empty() && index != "0")
      name += " #" + index;
    rows.rows.push_back({name + ": " + text_ranges(d), depth});
  }

  // A subFont, firstFont or secondFont element wraps exactly one font.
  void wrapper(const xml::Element& w, int depth) {
    if (depth > MaxTreeDepth) {
      out.errors.push_back("Complex font tree nests deeper than " + std::to_string(MaxTreeDepth) + " levels");
      return;
    }
    for (const xml::Element& f : w.children) {
      ctx.spend(BitmapFontSize, "font tree");
      const std::string_view kind = f.local_name();
      if (kind == "bitmapFont") {
        ++files;
        rows.rows.push_back({"Bitmap font " + text_path(f) + ": " + text_ranges(f), depth});
      } else if (kind == "pairFont") {
        rows.rows.push_back({"Pair font", depth});
        for (const std::string_view side : {"firstFont", "secondFont"})
          if (const xml::Element* c = f.child(side))
            wrapper(*c, depth + 1);
      } else if (kind == "multiScalableFont" || kind == "scalableFont") {
        std::string row = "Scalable font, size " + f.attribute_or("size", "?");
        if (const std::string alt = f.attribute_or("alternateChar"); !alt.empty() && alt != "0")
          row += ", alternate character " + alt;
        rows.rows.push_back({std::move(row), depth});
        if (const xml::Element* set = f.child("scalableFontDescriptionSet"))
          for (const xml::Element* d : set->children_named("scalableFontDescription"))
            face(*d, depth + 1);
        for (const xml::Element* d : f.children_named("scalableFontDescription"))
          face(*d, depth + 1);
      } else if (kind != "comment") {
        out.warnings.push_back("Complex font element " + std::string(kind) + " is not recognized");
      }
    }
  }
};

}  // namespace

bool probe_text(Bytes data) {
  return xml::root_name(data) == TextRootName;
}

void analyze_text(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(TextName);
  const xml::Element root = parse_text(data, ctx);
  Section& s = out.section(std::string(TextName));
  if (const std::string* v = root.attribute("version"))
    s.add("Version", *v);

  Group fonts("Font Tree");
  TextWalker w{fonts, 0, out, ctx};
  if (const xml::Element* sub = root.child("subFont"))
    w.wrapper(*sub, 0);
  else
    out.errors.push_back("Complex font has no subFont");
  fonts.summary = count_of(w.files, "font file");
  s.add("Font Files", std::to_string(w.files));
  add_groups(out, std::string(TextName) + " Contents", {fonts});
}

}  // namespace nnfck::fcpx
