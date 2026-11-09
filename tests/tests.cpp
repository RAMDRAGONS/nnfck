#include <algorithm>
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
  check(!a.errors.empty() && a.format == "Sead Yaz (corrupt)", "impossible size is reported without decoding");
}

void test_analysis_format_names() {
  const Analysis empty = analyze(yaz_file(0, {}), Context{});
  check(empty.format == "Sead Yaz-compressed empty data", "empty payload naming");

  const Analysis unknown = analyze(yaz_file(3, {0xE0, 'a', 'b', 'c'}), Context{});
  check(unknown.format == "Sead Yaz-compressed unknown data", "unknown payload naming");

  // A Yaz stream wrapping a minimal big-endian BYML header with no root node.
  const Analysis byml = analyze(yaz_file(16, {0xFF, 'B', 'Y', 0, 2, 0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0}),
                                Context{});
  check(byml.format == "Sead Yaz-compressed Sead Binary YAML", "nested payload naming");
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

// An ELink database with one user owning one zeroed record.
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

// An SLink database with one user holding one event and its asset.
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

// An eflk ELink database with one user holding one call table and effect.
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
  check(eflk.format == "Binary ELink Database" && eflk.errors.empty() && eflk.warnings.empty(),
        "well-formed eflk ELink database is clean");
  check(!analyze(eflk_file(0x10), Context{}).errors.empty(), "eflk user table mismatch is reported");

  const Analysis slink = analyze(slink_file(0x3C + 0xC + 0x44 + 4), Context{});
  check(slink.format == "Binary SLink Database" && slink.errors.empty() && slink.warnings.empty(),
        "well-formed SLink database is clean");
  check(!analyze(slink_file(0x10), Context{}).errors.empty(), "SLink user size mismatch is reported");

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
  check(elink.format == "Binary ELink Database" && elink.errors.empty() && elink.warnings.empty(),
        "well-formed ELink database is clean");
  check(analyze(elink_file("Player", 0), Context{}).warnings.size() == 1, "ELink user hash mismatch is reported");
}

void test_zstd() {
  const std::vector<u8> byml{'B', 'Y', 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  const Analysis a = analyze(zstd_frame(byml), Context{});
  check(a.format == "Zstandard-compressed Sead Binary YAML" && a.errors.empty(), "zstd payload naming");

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

  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "All tests passed\n";
  return 0;
}
