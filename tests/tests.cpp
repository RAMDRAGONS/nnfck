#include <cstdio>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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
  check(byml.format == "Sead Yaz-compressed Binary YAML", "nested payload naming");
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

// CRC-32 (zlib polynomial) of the decoded payload, for comparison against an
// independent decoder.
u32 crc32(const std::vector<u8>& data) {
  static const auto table = [] {
    std::vector<u32> t(256);
    for (u32 i = 0; i < 256; ++i) {
      u32 c = i;
      for (int k = 0; k < 8; ++k)
        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  u32 c = 0xFFFFFFFFu;
  for (const u8 b : data)
    c = table[(c ^ b) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

int hash_files(int argc, char** argv) {
  int status = 0;
  for (int i = 2; i < argc; ++i) {
    try {
      const auto file = read_file(std::filesystem::path(argv[i]));
      yaz::StreamStats st;
      const auto out = decode(file, st);
      std::printf("%08x %s\n", crc32(out), argv[i]);
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

  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "All tests passed\n";
  return 0;
}
