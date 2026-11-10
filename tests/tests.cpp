#include <algorithm>
#include <bit>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/crc32.hpp"
#include "core/file.hpp"
#include "core/json.hpp"
#include "formats/format.hpp"
#include "formats/yaz/yaz.hpp"

using namespace nnfck;

namespace {

int g_failures = 0;

void check(bool ok, std::string_view what) {
  if (!ok) {
    ++g_failures;
    std::cerr << "FAIL: " << what << '\n';
  }
}

std::vector<u8> yaz_file(u32 decompressed_size, std::initializer_list<u8> stream, u32 alignment = 0) {
  std::vector<u8> v = {'Y', 'a', 'z', '0'};
  for (int shift = 24; shift >= 0; shift -= 8)
    v.push_back(static_cast<u8>(decompressed_size >> shift));
  for (int shift = 24; shift >= 0; shift -= 8)
    v.push_back(static_cast<u8>(alignment >> shift));
  v.insert(v.end(), 4, 0);
  v.insert(v.end(), stream);
  return v;
}

std::vector<u8> decode(const std::vector<u8>& file, yaz::StreamStats& st) {
  std::vector<u8> out(yaz::read_header(file).decompressed_size);
  yaz::decompress(file, out, st);
  return out;
}

bool throws_parse_error(const std::function<void()>& fn) {
  try {
    fn();
  } catch (const ParseError&) {
    return true;
  }
  return false;
}

void test_literals() {
  const auto file = yaz_file(3, {0xE0, 'a', 'b', 'c'});
  yaz::StreamStats st;
  const auto out = decode(file, st);
  check(out == std::vector<u8>{'a', 'b', 'c'}, "literals decode");
  check(st.literals == 3 && st.groups == 1 && st.stream_end == file.size(), "literal stats");
}

void test_overlapping_short_match() {
  // One literal, then distance 1 length 17 (nibble 0xF): a run of 18 'x'.
  const auto file = yaz_file(18, {0x80, 'x', 0xF0, 0x00});
  yaz::StreamStats st;
  const auto out = decode(file, st);
  check(out == std::vector<u8>(18, 'x'), "overlapping short match decodes");
  check(st.short_matches == 1 && st.overlapping_matches == 1 && st.longest_match == 17, "short match stats");
}

void test_long_match_bounds() {
  // Nibble 0 selects a third byte: length = byte + 0x12, so 0x00 -> 18 and 0xFF -> 273.
  const auto min_file = yaz_file(19, {0x80, 'y', 0x00, 0x00, 0x00});
  yaz::StreamStats st;
  check(decode(min_file, st) == std::vector<u8>(19, 'y'), "minimum 3-byte match");
  check(st.long_matches == 1 && st.longest_match == 18, "minimum 3-byte match stats");

  const auto max_file = yaz_file(274, {0x80, 'z', 0x00, 0x00, 0xFF});
  yaz::StreamStats st2;
  check(decode(max_file, st2) == std::vector<u8>(274, 'z'), "maximum 3-byte match");
  check(st2.longest_match == 273, "maximum 3-byte match stats");
}

void test_distance() {
  // "abcd" then distance 4 length 4 (pair 0x2003).
  const auto file = yaz_file(8, {0xF0, 'a', 'b', 'c', 'd', 0x20, 0x03});
  yaz::StreamStats st;
  check(decode(file, st) == std::vector<u8>{'a', 'b', 'c', 'd', 'a', 'b', 'c', 'd'}, "distance decode");
  check(st.farthest_distance == 4 && st.overlapping_matches == 0, "distance stats");
}

void test_multi_group() {
  const auto file = yaz_file(9, {0xFF, '1', '2', '3', '4', '5', '6', '7', '8', 0x80, '9'});
  yaz::StreamStats st;
  check(decode(file, st) == std::vector<u8>{'1', '2', '3', '4', '5', '6', '7', '8', '9'}, "multi-group decode");
  check(st.groups == 2, "multi-group stats");
}

void test_errors() {
  yaz::StreamStats st;
  check(throws_parse_error([&] { decode(yaz_file(4, {0x00, 0x10, 0x00}), st); }),
        "back-reference before start is rejected");
  check(throws_parse_error([&] { decode(yaz_file(4, {0xE0, 'a', 'b'}), st); }), "truncated literal is rejected");
  check(throws_parse_error([&] { decode(yaz_file(4, {0x80, 'a', 0xF0}), st); }),
        "truncated back-reference is rejected");
  check(throws_parse_error([&] { decode(yaz_file(5, {0x80, 'a', 0xF0, 0x00}), st); }),
        "match overrunning decompressed size is rejected");
  check(throws_parse_error([&] { yaz::read_header(std::vector<u8>{'Y', 'a', 'z', '0', 0}); }),
        "short header is rejected");
}

void test_partial_progress_on_error() {
  yaz::StreamStats st;
  try {
    decode(yaz_file(4, {0xE0, 'a', 'b'}), st);
  } catch (const ParseError&) {
  }
  check(st.bytes_written == 2 && st.literals == 2, "stats reflect progress up to the failure");
}

void test_expansion_limit() {
  check(yaz::max_decompressed_size(0x10) == 0, "header-only stream decodes nothing");
  check(yaz::max_decompressed_size(0x10 + 25) == 8 * 273, "one full group limit");
  check(yaz::max_decompressed_size(0x10 + 26) == 16 * 273, "partial second group limit");

  const auto file = yaz_file(0x10000000, {0x00});
  const Analysis a = analyze(file, Context{});
  check(!a.errors.empty() && a.format == "Yaz (corrupt)", "impossible size is reported without decoding");
}

void test_analysis_format_names() {
  const Analysis empty = analyze(yaz_file(0, {}), Context{});
  check(empty.format == "Yaz-compressed empty data", "empty payload naming");

  const Analysis unknown = analyze(yaz_file(3, {0xE0, 'a', 'b', 'c'}), Context{});
  check(unknown.format == "Yaz-compressed unknown data", "unknown payload naming");

  // A Yaz stream wrapping a minimal big-endian BYML header with no root node.
  const Analysis byml = analyze(yaz_file(16, {0xFF, 'B', 'Y', 0, 2, 0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0}),
                                Context{});
  check(byml.format == "Yaz-compressed Binary YAML", "nested payload naming");
  check(byml.errors.empty(), "nested payload has no errors");
}

void test_trailing_bytes() {
  const auto file = yaz_file(1, {0x80, 'a', 0, 0, 0});
  const Analysis zero = analyze(file, Context{});
  check(zero.warnings.empty(), "zero trailing padding is not a warning");

  const auto dirty = yaz_file(1, {0x80, 'a', 0, 7});
  const Analysis nonzero = analyze(dirty, Context{});
  check(nonzero.warnings.size() == 1, "non-zero trailing bytes are a warning");
}

void put_u16(std::vector<u8>& v, std::size_t at, u16 x) {
  v[at] = static_cast<u8>(x);
  v[at + 1] = static_cast<u8>(x >> 8);
}

void put_u32(std::vector<u8>& v, std::size_t at, u32 x) {
  for (std::size_t i = 0; i < 4; ++i)
    v[at + i] = static_cast<u8>(x >> (8 * i));
}

// Little-endian SARC with unnamed entries that each span `member_size` bytes of
// zeroed data, laid out back to back after the name table.
std::vector<u8> sarc_file(u16 count, u32 member_size, u32 data_offset_override = 0) {
  const std::size_t fat = 0x14, entries = fat + 0xC, fnt = entries + std::size_t(count) * 0x10;
  const u32 data_offset = data_offset_override != 0 ? data_offset_override : static_cast<u32>(fnt + 8);
  std::vector<u8> v(std::max<std::size_t>(fnt + 8, data_offset) + std::size_t(count) * member_size);
  std::copy_n("SARC", 4, v.begin());
  put_u16(v, 0x4, 0x14);
  v[0x6] = 0xFF;
  v[0x7] = 0xFE;
  put_u32(v, 0x8, static_cast<u32>(v.size()));
  put_u32(v, 0xC, data_offset);
  put_u16(v, 0x10, 0x100);
  std::copy_n("SFAT", 4, v.begin() + static_cast<std::ptrdiff_t>(fat));
  put_u16(v, fat + 0x4, 0xC);
  put_u16(v, fat + 0x6, count);
  put_u32(v, fat + 0x8, 0x65);
  for (u16 i = 0; i < count; ++i) {
    const std::size_t e = entries + std::size_t(i) * 0x10;
    put_u32(v, e, i);
    put_u32(v, e + 0x8, i * member_size);
    put_u32(v, e + 0xC, (i + 1u) * member_size);
  }
  std::copy_n("SFNT", 4, v.begin() + static_cast<std::ptrdiff_t>(fnt));
  put_u16(v, fnt + 0x4, 0x8);
  return v;
}

void test_sanitize() {
  check(sanitize("\x1B[31mred") == "\\x1B[31mred", "ESC is escaped");
  check(sanitize("a\nb\x7F") == "a\\x0Ab\\x7F", "C0 controls and DEL are escaped");
  check(sanitize("\xE6\x97\xA5\xE6\x9C\xAC") == "\xE6\x97\xA5\xE6\x9C\xAC", "well-formed UTF-8 is kept");
  check(sanitize("\xC2\x9B") == "\\xC2\\x9B", "C1 control (CSI) is escaped");
  check(sanitize("\x9B") == "\\x9B", "lone 8-bit CSI is escaped");
  check(sanitize("\xC0\xAF") == "\\xC0\\xAF", "overlong encoding is escaped");
  check(sanitize("\xED\xA0\x80") == "\\xED\\xA0\\x80", "surrogate is escaped");
}

void test_sarc_rejects_overlapping_data() {
  // Data offset 0 would let members span the archive's own headers.
  const auto file = sarc_file(1, 4, 0);
  auto bad = file;
  put_u32(bad, 0xC, 0);
  const Analysis a = analyze(bad, Context{});
  check(!a.errors.empty() && a.sections.size() == 1, "data offset inside the headers is rejected");
  check(analyze(file, Context{}).errors.empty(), "well-formed SARC has no errors");
}

void test_members_listed_one_layer_deep() {
  const auto file = sarc_file(2, 0x10);
  check(analyze(file, Context{}).sections.size() == 2, "input SARC lists its members");
  check(analyze(file, Context{}.member()).sections.size() == 1, "member SARC is identified without a listing");
}

void test_budget() {
  const auto file = sarc_file(4, 0x100);
  Budget budget{file.size() + 0x180};
  const Analysis a = analyze(file, Context{0, &budget});
  std::size_t not_analyzed = 0;
  for (const Section& s : a.sections)
    for (const Field& f : s.fields())
      if (f.value.find("[Not analyzed") != std::string::npos)
        ++not_analyzed;
  check(not_analyzed == 3, "members beyond the budget are not analyzed");

  Budget spent{0x10};
  check(throws_parse_error([&] { Context{0, &spent}.spend(0x11, "test"); }), "work beyond the budget stops a parse");
  check(!throws_parse_error([&] { Context{0, &spent}.spend(0x10, "test"); }), "work within the budget proceeds");

  Budget tiny{0x10};
  const auto yaz = yaz_file(0x100, {0x80, 'a', 0x00, 0x00, 0xFF});
  const Analysis b = analyze(yaz, Context{0, &tiny});
  check(!b.errors.empty(), "decompression beyond the budget is refused");
}

void test_ptcl_overlapping_siblings() {
  std::vector<u8> v(0x80);
  std::copy_n("VFXB    ", 8, v.begin());
  v[0xC] = 0xFF;
  v[0xD] = 0xFE;
  put_u16(v, 0x16, 0x40);
  put_u32(v, 0x1C, static_cast<u32>(v.size()));
  std::copy_n("ESTA", 4, v.begin() + 0x40);
  put_u32(v, 0x48, 0xFFFFFFFF);
  put_u32(v, 0x4C, 1);
  const Analysis a = analyze(v, Context{});
  check(!a.errors.empty(), "overlapping effect node siblings are rejected");
}

// One Zstandard frame holding `payload` as a single raw block. Without a
// declared size the frame needs a window descriptor instead.
std::vector<u8> zstd_frame(const std::vector<u8>& payload, bool declare_size = true, u8 dict_id = 0) {
  std::vector<u8> v{0x28, 0xB5, 0x2F, 0xFD};
  if (declare_size) {
    v.push_back(static_cast<u8>(0x20 | (dict_id != 0 ? 0x01 : 0x00)));
    if (dict_id != 0)
      v.push_back(dict_id);
    v.push_back(static_cast<u8>(payload.size()));
  } else {
    v.push_back(0x00);
    v.push_back(0x00);
  }
  const u32 block = static_cast<u32>(payload.size()) << 3 | 1;
  v.push_back(static_cast<u8>(block));
  v.push_back(static_cast<u8>(block >> 8));
  v.push_back(static_cast<u8>(block >> 16));
  v.insert(v.end(), payload.begin(), payload.end());
  return v;
}

std::string field(const Analysis& a, std::string_view key) {
  for (const Section& s : a.sections)
    for (const Field& f : s.fields())
      if (f.key == key)
        return f.value;
  return {};
}

void put_u32_le(std::vector<u8>& v, std::size_t at, u32 x) {
  for (int i = 0; i < 4; ++i)
    v[at + static_cast<std::size_t>(i)] = static_cast<u8>(x >> (8 * i));
}

bool has_row(const Analysis& a, std::string_view text) {
  for (const Section& s : a.sections)
    for (const Field& f : s.fields())
      if (f.key.empty() && f.value == text)
        return true;
  return false;
}

void put_f32_le(std::vector<u8>& v, std::size_t at, float x) {
  put_u32_le(v, at, std::bit_cast<u32>(x));
}

void put_text(std::vector<u8>& v, std::size_t at, std::string_view text) {
  std::copy(text.begin(), text.end(), v.begin() + static_cast<std::ptrdiff_t>(at));
}

// A little-endian layout block: kind, size, then `body` padded to four bytes.
std::vector<u8> layout_block(std::string_view kind, std::vector<u8> body) {
  body.resize((body.size() + 3) & ~std::size_t{3});
  std::vector<u8> v(8);
  put_text(v, 0, kind);
  put_u32_le(v, 4, static_cast<u32>(8 + body.size()));
  v.insert(v.end(), body.begin(), body.end());
  return v;
}

std::vector<u8> layout_file(std::string_view magic, u32 version, const std::vector<std::vector<u8>>& blocks) {
  std::vector<u8> v(0x14);
  put_text(v, 0, magic);
  v[4] = 0xFF;
  v[5] = 0xFE;
  put_u16(v, 6, 0x14);
  put_u32_le(v, 8, version);
  for (const auto& b : blocks)
    v.insert(v.end(), b.begin(), b.end());
  put_u32_le(v, 0xC, static_cast<u32>(v.size()));
  v[0x10] = static_cast<u8>(blocks.size());
  return v;
}

std::vector<u8> pane_body(std::string_view name) {
  std::vector<u8> body(0x4C);
  put_text(body, 4, name);
  return body;
}

// Groups widened their name from 24 bytes to 33 plus padding.
std::vector<u8> group_body(std::string_view name, std::string_view pane, bool legacy) {
  const std::size_t header = legacy ? 0x1C : 0x24;
  std::vector<u8> body(header + 24);
  put_text(body, 0, name);
  put_u16(body, legacy ? 24 : 34, 1);
  put_text(body, header, pane);
  return body;
}

std::vector<u8> flyt_file(u32 version, bool legacy_groups, bool close_tree) {
  std::vector<u8> layout(0x14 + 8);
  layout[0] = 1;
  put_f32_le(layout, 4, 1280);
  put_f32_le(layout, 8, 720);
  put_text(layout, 0x14, "Test");
  std::vector<std::vector<u8>> blocks{layout_block("lyt1", layout), layout_block("pan1", pane_body("RootPane")),
                                      layout_block("pas1", {}), layout_block("pic1", pane_body("P_Child"))};
  if (close_tree)
    blocks.push_back(layout_block("pae1", {}));
  blocks.push_back(layout_block("grp1", group_body("RootGroup", "P_Child", legacy_groups)));
  return layout_file("FLYT", version, blocks);
}

std::vector<u8> flan_file() {
  std::vector<u8> tag(0x18 + 4 + 36);
  put_u16(tag, 2, 1);
  put_u32_le(tag, 4, 0x20);
  put_u32_le(tag, 8, 0x24);
  put_u16(tag, 0x12, 10);
  put_text(tag, 0x18, "In");
  put_text(tag, 0x1C, "G_All");
  std::vector<u8> anim(0x3C - 8 + 8);
  put_u16(anim, 0, 10);
  put_u16(anim, 6, 1);
  put_u32_le(anim, 8, 0x14);
  put_u32_le(anim, 0xC, 0x18);
  put_text(anim, 0x10, "N_Root");
  anim[0x10 + 28] = 1;
  put_u32_le(anim, 0x10 + 32, 0x24);
  put_text(anim, 0x34, "FLPA");
  return layout_file("FLAN", 0x08000000, {layout_block("pat1", tag), layout_block("pai1", anim)});
}

// A pair font joining a bitmap font and one scalable face limited to ASCII.
std::vector<u8> fcpx_file() {
  std::vector<u8> v(0x94);
  put_text(v, 0, "FCPX");
  v[4] = 0xFF;
  v[5] = 0xFE;
  put_u16(v, 6, 0x14);
  put_u32_le(v, 8, 0x09000000);
  put_u32_le(v, 0xC, 0x94);
  v[0x10] = 1;
  put_u32_le(v, 0x14, 4);
  put_u32_le(v, 0x18, 2);
  put_u32_le(v, 0x1C, 0xC);
  put_u32_le(v, 0x20, 0x1C);
  put_u32_le(v, 0x28, 0x7C - 0x24);
  put_u32_le(v, 0x34, 1);
  put_f32_le(v, 0x38, 20);
  put_u32_le(v, 0x40, 1);
  put_u32_le(v, 0x44, 0x20);
  put_u32_le(v, 0x54 + 0xC, 0x84 - 0x34);
  put_u32_le(v, 0x54 + 0x20, 1);
  put_u32_le(v, 0x54 + 0x24, 0x8C - 0x34);
  put_text(v, 0x7C, "a.bffnt");
  put_text(v, 0x84, "b.bfttf");
  put_u32_le(v, 0x8C, 0x20);
  put_u32_le(v, 0x90, 0x7E);
  return v;
}

// A big-endian layout image: 0x200 bytes of surface, then the footer.
std::vector<u8> flim_file() {
  std::vector<u8> v(0x200 + 0x28);
  const auto be32 = [&](std::size_t at, u32 x) {
    for (int i = 0; i < 4; ++i)
      v[at + static_cast<std::size_t>(i)] = static_cast<u8>(x >> (24 - 8 * i));
  };
  const std::size_t h = 0x200;
  put_text(v, h, "FLIM");
  v[h + 4] = 0xFE;
  v[h + 5] = 0xFF;
  v[h + 7] = 0x14;
  be32(h + 8, 0x02020000);
  be32(h + 0xC, static_cast<u32>(v.size()));
  v[h + 0x11] = 1;
  put_text(v, h + 0x14, "imag");
  be32(h + 0x18, 0x10);
  v[h + 0x1D] = 64;
  v[h + 0x1F] = 32;
  v[h + 0x21] = 0x20;
  v[h + 0x22] = 12;
  v[h + 0x23] = 4 | (3 << 5);
  be32(h + 0x24, 0x200);
  return v;
}

std::vector<u8> text_file(std::string_view text) {
  return std::vector<u8>(text.begin(), text.end());
}

void test_layout_sources() {
  const Analysis image = analyze(flim_file(), Context{});
  check(image.sections.front().fields().front().key == "Library" && field(image, "Library") == "NintendoWare",
        "the library is the first field");
  check(image.format == "Cafe Layout Image (Wii U)" && image.errors.empty() && image.warnings.empty() &&
            field(image, "Width") == "64" && field(image, "Height") == "32" && field(image, "Format") == "BC1" &&
            field(image, "Swizzle") == "3",
        "layout image footer is read");

  const Analysis lyt = analyze(text_file(R"(<?xml version="1.0" encoding="utf-8"?>
<nw4f_layout version="1.5.12" xmlns="http://www.nintendo.co.jp/NW4F/LayoutEditor">
  <head><generator name="Editor" version="2.0" /></head>
  <body>
    <lyt>
      <paneSet>
        <pane kind="Picture" name="P_A"><picture><material name="P_A" /></picture></pane>
        <pane kind="Parts" name="L_B"><parts path="Btn&amp;00.flyt" /></pane>
      </paneSet>
      <paneHierarchy><paneTree name="RootPane"><paneTree name="P_A" /><paneTree name="L_B" /></paneTree></paneHierarchy>
      <groupSet><group name="RootGroup"><paneRef name="P_A" /></group></groupSet>
      <screenSetting origin="Classic"><layoutSize x="1280" y="720" /></screenSetting>
      <control name="Button" userName="Main" />
      <textureFile imagePath="a.tga" format="BC1" />
    </lyt>
  </body>
</nw4f_layout>)"),
                               Context{});
  check(lyt.format == "Cafe Layout" && lyt.errors.empty() && lyt.warnings.empty() &&
            field(lyt, "Origin") == "Top left" && field(lyt, "Control") == "Button (Main)" &&
            field(lyt, "Panes") == "2 (1 Picture, 1 Parts)" && has_row(lyt, "L_B: Parts (Btn&00.flyt)") &&
            has_row(lyt, "RootPane: Null") && has_row(lyt, "RootGroup: 1 pane"),
        "layout source panes, parts and groups are listed");

  const Analysis anim = analyze(text_file(R"(<nw4f_layout version="1.0"><body separateMode="true">
  <animTag name="In" startFrame="0" endFrame="10" animLoop="OneTime"><group name="G_A" />
    <lan animType="PaneSRT" startFrame="0" endFrame="10"><animContent name="P_A" /></lan>
  </animTag>
  <lan animType="Visibility" startFrame="0" endFrame="10"><animContent name="P_A" /></lan>
</body></nw4f_layout>)"),
                                Context{});
  check(anim.format == "Cafe Layout Animation" && anim.errors.empty() && field(anim, "Separate Mode") == "Yes" &&
            has_row(anim, "In: frames 0 to 10, OneTime, 1 group (G_A)") && has_row(anim, "P_A: PaneSRT, Visibility"),
        "layout animation source tags and targets are listed");

  const Analysis font = analyze(text_file(R"(<complexFontDescription version="1.0"><subFont><pairFont>
  <firstFont><bitmapFont><path>a.ffnt</path></bitmapFont></firstFont>
  <secondFont><multiScalableFont size="20"><scalableFontDescriptionSet>
    <scalableFontDescription><path>b.ttf</path>
      <charCodeRangeSet><charCodeRange first="32" last="126" /></charCodeRangeSet>
    </scalableFontDescription>
  </scalableFontDescriptionSet></multiScalableFont></secondFont>
</pairFont></subFont></complexFontDescription>)"),
                                Context{});
  check(font.format == "Cafe Complex Font" && font.errors.empty() && font.warnings.empty() &&
            field(font, "Font Files") == "2" && has_row(font, "Bitmap font a.ffnt: all characters") &&
            has_row(font, "b.ttf: 0x0020 to 0x007E"),
        "complex font source tree is walked");

  check(!analyze(text_file("<nw4f_layout><body><lyt></body></nw4f_layout>"), Context{}).errors.empty(),
        "mismatched XML tags are reported");
}

// An intermediate model, with the binary variant's stream table appended
// after a NUL when `binary` is set.
std::vector<u8> intermediate_file(bool binary) {
  std::string xml = R"(<?xml version="1.0" encoding="utf-8"?>
<nw4f_3dif version="4.0.0">
<file_info><create tool_name="Exporter" tool_version="1.0" date_time="2020-01-01T00:00:00" /></file_info>
<model>
<model_info material_count="1" bone_count="1" />
<material_array length="1"><material index="0" name="mat0" /></material_array>
<skeleton><bone_array length="1"><bone index="0" name="root" /></bone_array></skeleton>
</model>
</nw4f_3dif>)";
  std::vector<u8> v(xml.begin(), xml.end());
  if (!binary)
    return v;
  v.resize((v.size() + 4 + 31) & ~std::size_t{31});
  const std::size_t table = v.size();
  v.resize(table + 0x20 + 0x18 + 8);
  put_text(v, table, "g3dstrma");
  put_u32_le(v, table + 8, 1);
  put_u32_le(v, table + 0xC, 0x20);
  put_u32_le(v, table + 0x10, 0x18 + 8);
  put_text(v, table + 0x20, "stream  ");
  put_u32_le(v, table + 0x28, 0);
  put_u32_le(v, table + 0x2C, 2);
  put_u32_le(v, table + 0x30, 1);
  put_u32_le(v, table + 0x34, 8);
  return v;
}

void test_intermediates() {
  const Analysis text = analyze(intermediate_file(false), Context{});
  check(text.format == "Cafe Model" && text.errors.empty() && text.warnings.empty() &&
            field(text, "Material Count") == "1" && field(text, "Created With") == "Exporter 1.0" &&
            has_row(text, "mat0") && has_row(text, "root"),
        "intermediate model is summarized");
  const Analysis binary = analyze(intermediate_file(true), Context{});
  check(binary.format == "Cafe Model Binary" && binary.errors.empty() && field(binary, "Streams") == "1" &&
            has_row(binary, "float, 2 values, 0x8 bytes"),
        "binary intermediate streams are read");

  const Analysis cafe = analyze(text_file(R"(<?xml version="1.0"?>
<EmitterSet><Version>1.8.0.0</Version><EmitterList>
  <Emitter><name>Smoke</name><type>Complex</type><texture>smoke.ftxb</texture></Emitter>
</EmitterList></EmitterSet>)"),
                                Context{});
  check(cafe.format == "Emitter Set" && cafe.errors.empty() && field(cafe, "Version") == "1.8.0.0" &&
            has_row(cafe, "Smoke: Complex") && has_row(cafe, "smoke.ftxb"),
        "Wii U emitter set source is listed");
  const Analysis nx = analyze(text_file(R"(<EmitterSetData Version="1.0.0.7"><EmitterList>
  <EmitterData><Name>Parent</Name><Textures><Texture0><EmitterTextureFileData><FilePath>a.ftxb</FilePath>
  </EmitterTextureFileData></Texture0></Textures>
    <EmitterList><EmitterData><Name>Child</Name></EmitterData></EmitterList>
  </EmitterData>
</EmitterList></EmitterSetData>)"),
                              Context{});
  check(nx.format == "Emitter Set" && nx.errors.empty() && field(nx, "Emitters") == "2" &&
            has_row(nx, "Child") && has_row(nx, "a.ftxb"),
        "Switch emitter set source nests child emitters");
}

void test_layouts() {
  const Analysis lyt = analyze(flyt_file(0x08000000, false, true), Context{});
  check(lyt.format == "Binary Cafe Layout (Switch)" && lyt.errors.empty() && lyt.warnings.empty() &&
            field(lyt, "Panes") == "2 (1 Null, 1 Picture)" && field(lyt, "Name") == "Test" &&
            has_row(lyt, "P_Child: Picture") && has_row(lyt, "RootGroup: 1 pane"),
        "layout panes and groups are listed");
  const Analysis legacy = analyze(flyt_file(0x02020000, true, true), Context{});
  check(legacy.errors.empty() && has_row(legacy, "RootGroup: 1 pane"), "layout groups with 24-byte names are read");
  check(!analyze(flyt_file(0x08000000, false, false), Context{}).errors.empty(), "unclosed pane tree is reported");

  const Analysis anim = analyze(flan_file(), Context{});
  check(anim.format == "Binary Cafe Layout Animation (Switch)" && anim.errors.empty() && anim.warnings.empty() &&
            field(anim, "Tag") == "In" && field(anim, "Tag Frames") == "0 to 10" && has_row(anim, "G_All") &&
            has_row(anim, "N_Root (pane): Pane SRT"),
        "layout animation tag and targets are listed");

  const Analysis font = analyze(fcpx_file(), Context{});
  check(font.format == "Binary Cafe Complex Font (Switch)" && font.errors.empty() && font.warnings.empty() &&
            field(font, "Font Files") == "2" && has_row(font, "Bitmap font a.bffnt: all characters") &&
            has_row(font, "b.bfttf: 0x0020 to 0x007E"),
        "complex font tree is walked");
}

// An XLink database with no users and one asset parameter named `param`.
std::vector<u8> xlink_file(std::string_view param) {
  std::vector<u8> v(0x48 + 0x14 + 0xC);
  std::copy_n("XLNK", 4, v.begin());
  put_u32_le(v, 0x8, 30);
  put_u32_le(v, 0x48, static_cast<u32>(0x14 + 0xC + param.size() + 1));
  put_u32_le(v, 0x50, 1);
  put_u32_le(v, 0x5C + 4, 4);
  put_u32_le(v, 0x44, static_cast<u32>(v.size()));
  v.insert(v.end(), param.begin(), param.end());
  v.push_back(0);
  put_u32_le(v, 0x4, static_cast<u32>(v.size()));
  return v;
}

// An XLink database with 64-bit positions, no users and one asset
// parameter: a 0x60-byte header, the parameter table header, then one
// 24-byte definition aligned to 8.
std::vector<u8> xlink_wide_file() {
  const u32 table = 0x60, defines = table + 0x18, strings = defines + 0x18;
  std::vector<u8> v(strings);
  std::copy_n("XLNK", 4, v.begin());
  put_u32_le(v, 0x8, 33);
  put_u32_le(v, 0x58, strings);
  put_u32_le(v, table, strings + 7 - table);
  put_u32_le(v, table + 0x8, 1);
  put_u32_le(v, defines + 0x8, 4);
  for (const char c : std::string_view("Volume"))
    v.push_back(static_cast<u8>(c));
  v.push_back(0);
  put_u32_le(v, 0x4, static_cast<u32>(v.size()));
  return v;
}

// A resource version 21 XLink database: one named user and one asset
// parameter, with names in a shared table after the definitions.
std::vector<u8> xlink21_file() {
  const u32 users = 0x4C, defines = users + 8, names = defines + 8;
  std::vector<u8> v(names);
  std::copy_n("XLNK", 4, v.begin());
  put_u32(v, 0x8, 21);
  put_u32(v, 0x10, 1);
  put_u32(v, 0x40, 1);
  put_u32(v, 0x48, names);
  put_u32(v, users + 4, 5);
  put_u32(v, defines + 4, 1);
  for (const char c : std::string_view("Eset\0Player", 11))
    v.push_back(static_cast<u8>(c));
  v.push_back(0);
  put_u32(v, 0x4, static_cast<u32>(v.size()));
  return v;
}

// An EffectLink database with one user owning one zeroed record.
std::vector<u8> elink_file(std::string_view user, u32 hash) {
  const u32 records = 0x20 + 0x14, strings = records + 0xCC;
  std::vector<u8> v(strings);
  std::copy_n("BLNK", 4, v.begin());
  put_u32(v, 0x8, 0x20);
  put_u32(v, 0xC, 1);
  put_u32(v, 0x10, records);
  put_u32(v, 0x14, 1);
  put_u32(v, 0x18, strings);
  put_u32(v, 0x20, hash);
  put_u32(v, 0x28, 1);
  put_u32(v, 0x2C, strings - 0x20);
  v.insert(v.end(), user.begin(), user.end());
  v.push_back(0);
  put_u32(v, 0x4, static_cast<u32>(v.size()));
  return v;
}

// A SoundLink database with one user holding one event and its asset.
std::vector<u8> slink_file(u32 block_size) {
  const u32 user = 0x14, tables = 0x3C + 0xC + 0x44;
  std::vector<u8> v(user + tables + 4);
  std::copy_n("SLNK", 4, v.begin());
  put_u32(v, 0x8, 50);
  put_u32(v, 0xC, 1);
  put_u32(v, 0x10, user);
  put_u32(v, user + 0x4, block_size);
  put_u32(v, user + 0x8, static_cast<u32>(v.size()) - 0x14);
  put_u32(v, user + 0xC, 1);
  put_u32(v, user + 0x14, 1);
  put_u32(v, user + 0x1C, tables);
  for (const char c : std::string_view("User"))
    v.push_back(static_cast<u8>(c));
  v.push_back(0);
  put_u32(v, 0x4, static_cast<u32>(v.size()));
  return v;
}

// An eflk EffectLink database with one user holding one call table and effect.
std::vector<u8> eflk_file(u32 pool) {
  const u32 user = 0x18, tables = 0x1C + 0x68 + 0x14;
  std::vector<u8> v(user + tables);
  std::copy_n("eflk", 4, v.begin());
  put_u32(v, 0x4, 9);
  put_u32(v, 0x8, 1);
  put_u32(v, 0xC, static_cast<u32>(v.size()));
  put_u32(v, 0x10, user);
  put_u32(v, user, 9);
  put_u32(v, user + 0x4, 1);
  put_u32(v, user + 0xC, 1);
  put_u32(v, user + 0x14, pool);
  for (const char c : std::string_view("User"))
    v.push_back(static_cast<u8>(c));
  v.push_back(0);
  return v;
}

void test_link_databases() {
  const Analysis eflk = analyze(eflk_file(0x1C + 0x68 + 0x14), Context{});
  check(eflk.format == "Binary EffectLink Database" && eflk.errors.empty() && eflk.warnings.empty(),
        "well-formed eflk EffectLink database is clean");
  check(!analyze(eflk_file(0x10), Context{}).errors.empty(), "eflk user table mismatch is reported");

  const Analysis slink = analyze(slink_file(0x3C + 0xC + 0x44 + 4), Context{});
  check(slink.format == "Binary SoundLink Database" && slink.errors.empty() && slink.warnings.empty(),
        "well-formed SoundLink database is clean");
  check(!analyze(slink_file(0x10), Context{}).errors.empty(), "SoundLink user size mismatch is reported");
  check(field(eflk, "Library") == "EffectLink" && field(slink, "Library") == "SoundLink",
        "link databases name their libraries");

  const Analysis effects = analyze(xlink_file("Eset"), Context{});
  check(effects.format == "Binary XLink Database (ELink2)" && effects.errors.empty() && effects.warnings.empty(),
        "XLink effect database is identified by its parameters");
  check(analyze(xlink_file("Volume"), Context{}).format == "Binary XLink Database (SLink2)",
        "XLink sound database is identified by its parameters");
  check(analyze(xlink_file("Other"), Context{}).format == "Binary XLink Database",
        "unrecognized XLink system is not named");

  const Analysis wide = analyze(xlink_wide_file(), Context{});
  check(wide.format == "Binary XLink Database (SLink2)" && wide.errors.empty() && wide.warnings.empty() &&
            field(wide, "Position Width") == "64-bit",
        "XLink database with 64-bit positions is read");

  const Analysis named = analyze(xlink21_file(), Context{});
  check(named.format == "Binary XLink Database (ELink2)" && named.errors.empty() && named.warnings.empty() &&
            named.sections.size() == 3,
        "XLink version 21 lists its named users");

  const Analysis elink = analyze(elink_file("Player", crc32("Player")), Context{});
  check(elink.format == "Binary EffectLink Database" && elink.errors.empty() && elink.warnings.empty(),
        "well-formed EffectLink database is clean");
  check(analyze(elink_file("Player", 0), Context{}).warnings.size() == 1, "EffectLink user hash mismatch is reported");
}

// A version 2 parameter archive: param_root holding the list Fog, whose
// object Fog0 holds a bool and a string parameter.
std::vector<u8> pmaa2_file(u32 string_offset_words = 3) {
  std::vector<u8> v(0x70);
  std::copy_n("AAMP", 4, v.begin());
  put_u32(v, 0x4, 2);
  put_u32(v, 0x8, 3);
  put_u32(v, 0xC, 0x70);
  put_u32(v, 0x14, 8);
  put_u32(v, 0x18, 2);
  put_u32(v, 0x1C, 1);
  put_u32(v, 0x20, 2);
  put_u32(v, 0x24, 4);
  put_u32(v, 0x28, 4);
  std::copy_n("aglenv", 6, v.begin() + 0x30);
  put_u32(v, 0x38, crc32("param_root"));
  put_u32(v, 0x3C, 1u << 16 | 3);
  put_u32(v, 0x44, crc32("Fog"));
  put_u32(v, 0x4C, 1u << 16 | 3);
  put_u32(v, 0x50, crc32("Fog0"));
  put_u32(v, 0x54, 2u << 16 | 2);
  put_u32(v, 0x58, crc32("enable"));
  put_u32(v, 0x5C, 4);
  put_u32(v, 0x60, crc32("name"));
  put_u32(v, 0x64, 7u << 24 | string_offset_words);
  put_u32(v, 0x68, 1);
  std::copy_n("fog", 3, v.begin() + 0x6C);
  return v;
}

// A version 1 parameter archive: param_root holding the object
// global_parameter with one single-byte bool.
std::vector<u8> pmaa1_file(u32 param_size = 0xD) {
  std::vector<u8> v(0x4D);
  std::copy_n("AAMP", 4, v.begin());
  put_u32(v, 0x4, 1);
  put_u32(v, 0x8, 1);
  put_u32(v, 0xC, 0x4D);
  put_u32(v, 0x14, 8);
  std::copy_n("gsdw", 4, v.begin() + 0x18);
  put_u32(v, 0x20, 0x2D);
  put_u32(v, 0x24, crc32("param_root"));
  put_u32(v, 0x2C, 1);
  put_u32(v, 0x30, 0x1D);
  put_u32(v, 0x34, 1);
  put_u32(v, 0x38, crc32("global_parameter"));
  put_u32(v, 0x40, param_size);
  put_u32(v, 0x48, crc32("enable"));
  v[0x4C] = 1;
  return v;
}

void test_parameter_archives() {
  const Analysis v2 = analyze(pmaa2_file(), Context{});
  check(v2.format == "Parameter Archive" && v2.errors.empty() && v2.warnings.empty(),
        "well-formed version 2 parameter archive is clean");
  check(field(v2, "Library") == "Advanced Graphics Library" && field(v2, "Extension") == "Binary Agl Environment (.baglenv)" && field(v2, "Parameter Types") == "1 bool, 1 string32",
        "version 2 parameter archive header fields");
  check(has_row(v2, "Fog: 0 lists, 1 object"), "parameter archive names resolve from the shipped list");

  check(!analyze(pmaa2_file(0xFFFFFF), Context{}).errors.empty(),
        "parameter value past the end of the archive is reported");
  auto miscounted = pmaa2_file();
  put_u32(miscounted, 0x20, 3);
  check(!analyze(miscounted, Context{}).warnings.empty(), "parameter archive header count mismatch is reported");
  auto future = pmaa2_file();
  put_u32(future, 0x4, 3);
  const Analysis unknown = analyze(future, Context{});
  check(unknown.errors.empty() && unknown.warnings.size() == 1, "unknown parameter archive version shows its header");

  const Analysis v1 = analyze(pmaa1_file(), Context{});
  check(v1.errors.empty() && v1.warnings.empty() && field(v1, "Extension") == "Binary Gsys Shadow (.bgsdw)" &&
            field(v1, "Parameters") == "1" && has_row(v1, "global_parameter: 1 parameter"),
        "well-formed version 1 parameter archive is clean");
  check(!analyze(pmaa1_file(0x40), Context{}).errors.empty(), "version 1 node past its parent is rejected");
}

std::vector<u8> text_bytes(std::string_view s) {
  return std::vector<u8>(s.begin(), s.end());
}

void test_parameter_archive_text() {
  const std::string curve = "\n9 7\n0.5 0.5 0.5\n0.5 1.0 1.0\n0.5 1.0 1.0\n";
  const std::string doc = R"(<?xml version="1.0" encoding="utf-8"?>
<root>
	<header type="agllmap" version="2" date="time stamp : 2012/09/07 18:29:41"/>
	<data>
		<param_list name="param_root">
			<param_list name="Fog">
				<param_array name="Fog0">
					<param name="enable" type="bool" value="true"/>
					<param name="Direction" type="vec3" value="0 -1 0"/>
					<param name="intensity0" type="curve1">)" + curve + R"(</param>
				</param_array>
			</param_list>
			<param_array name="config">
				<param name="type" type="string32" value="lightmap_array"/>
			</param_array>
		</param_list>
	</data>
</root>
)";
  const Analysis a = analyze(text_bytes(doc), Context{});
  check(a.format == "Parameter Archive" && a.errors.empty() && a.warnings.empty(), "text parameter archive is clean");
  check(field(a, "Library") == "Advanced Graphics Library" && field(a, "Extension") == "Agl Light Map (.agllmap)" &&
            field(a, "Data Version") == "2" && field(a, "Saved") == "2012/09/07 18:29:41" &&
            field(a, "Parameters") == "4" && field(a, "Parameter Types") == "1 bool, 1 vec3, 1 string32, 1 curve1",
        "text parameter archive header fields");
  check(has_row(a, "Fog: 0 lists, 1 object") && has_row(a, "config: 1 parameter"),
        "text parameter archive lists the root's members");

  std::string bad = doc;
  bad.replace(bad.find("0 -1 0"), 6, "0 -1");
  check(!analyze(text_bytes(bad), Context{}).errors.empty(), "text vec3 with two components is reported");
  std::string unknown = doc;
  unknown.replace(unknown.find("type=\"bool\""), 11, "type=\"bool2\"");
  check(!analyze(text_bytes(unknown), Context{}).errors.empty(), "text parameter of unknown type is reported");
  std::string short_curve = doc;
  short_curve.replace(short_curve.find("9 7\n"), 4, "12 7\n");
  check(!analyze(text_bytes(short_curve), Context{}).errors.empty(), "text curve missing points is reported");
}

void test_json() {
  const auto parse = [](std::string_view text) {
    return json::parse(Bytes(reinterpret_cast<const u8*>(text.data()), text.size()));
  };
  const json::Value doc = parse(R"({"a": [1.5, true, false, null, "xé😀\n"], "b": {}})");
  const json::Value* a = doc.member("a");
  check(a != nullptr && a->items.size() == 5 && a->items[0].number == 1.5 && a->items[1].boolean &&
            !a->items[2].boolean && a->items[3].is(json::Value::Kind::Null) &&
            a->items[4].string == "x\xC3\xA9\xF0\x9F\x98\x80\n",
        "JSON values and escapes decode");
  check(doc.member("b") != nullptr && doc.member("b")->is(json::Value::Kind::Object), "JSON nested object");
  check(throws_parse_error([&] { parse(R"({"a": 1} x)"); }), "JSON rejects trailing content");
  check(throws_parse_error([&] { parse(R"(["\ud83d"])"); }), "JSON rejects an unpaired surrogate");
  check(throws_parse_error([&] { parse(std::string(json::MaxDepth + 2, '[')); }), "JSON nesting is bounded");
  check(throws_parse_error([&] { parse("[1,]"); }), "JSON rejects a trailing comma");
}

void test_zstd() {
  const std::vector<u8> byml{'B', 'Y', 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  const Analysis a = analyze(zstd_frame(byml), Context{});
  check(a.format == "Zstandard-compressed Binary YAML" && a.errors.empty(), "zstd payload naming");

  const Analysis unsized = analyze(zstd_frame({'a', 'b', 'c'}, false), Context{});
  check(unsized.format == "Zstandard-compressed unknown data" && unsized.errors.empty(),
        "zstd frame without a declared size decodes");

  auto two = zstd_frame({'a', 'b', 'c'});
  const auto second = zstd_frame({'d', 'e'}, false);
  two.insert(two.end(), second.begin(), second.end());
  const Analysis multi = analyze(two, Context{});
  check(field(multi, "Frames") == "2" && field(multi, "Decompressed Size") == fmt_size(5) && multi.errors.empty(),
        "zstd frames are decoded in sequence");

  const Analysis dict = analyze(zstd_frame({'a'}, true, 7), Context{});
  check(dict.format == "Zstandard (not decompressed)" && !dict.errors.empty(), "zstd dictionary frames are refused");

  auto cut = zstd_frame({'a', 'b', 'c', 'd'});
  cut.pop_back();
  check(!analyze(cut, Context{}).errors.empty(), "truncated zstd frame is reported");

  auto padded = zstd_frame({'a'});
  padded.push_back(0);
  check(analyze(padded, Context{}).warnings.size() == 1, "bytes after the last zstd frame are a warning");

  Budget tiny{0x14};
  check(!analyze(zstd_frame(std::vector<u8>(0x40, 'x')), Context{0, &tiny}).errors.empty(),
        "zstd decompression beyond the budget is refused");
}

int hash_files(int argc, char** argv) {
  int status = 0;
  for (int i = 2; i < argc; ++i) {
    try {
      const auto file = read_file(std::filesystem::path(argv[i]));
      yaz::StreamStats st;
      const auto out = decode(file, st);
      std::printf("%08x %s\n", crc32(Bytes(out)), argv[i]);
    } catch (const std::exception& e) {
      std::printf("error %s: %s\n", argv[i], e.what());
      status = 1;
    }
  }
  return status;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--hash")
    return hash_files(argc, argv);

  test_literals();
  test_overlapping_short_match();
  test_long_match_bounds();
  test_distance();
  test_multi_group();
  test_errors();
  test_partial_progress_on_error();
  test_expansion_limit();
  test_analysis_format_names();
  test_trailing_bytes();
  test_sanitize();
  test_sarc_rejects_overlapping_data();
  test_members_listed_one_layer_deep();
  test_budget();
  test_ptcl_overlapping_siblings();
  test_zstd();
  test_link_databases();
  test_layouts();
  test_layout_sources();
  test_intermediates();
  test_parameter_archives();
  test_parameter_archive_text();
  test_json();

  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "All tests passed\n";
  return 0;
}
