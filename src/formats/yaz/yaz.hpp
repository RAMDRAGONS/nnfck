#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "formats/format.hpp"

// Sead Yaz: the LZ77 variant used by Nintendo's sead library on Wii U, 3DS and
// Switch. The magic's trailing digit dates back to the N64, where it told cart
// (0) from disk (1) data; it carries no meaning here, so it is shown verbatim
// rather than used as the format's name.
namespace nnfck::yaz {

inline constexpr std::string_view Name = "Sead Yaz";
inline constexpr std::string_view Magic = "Yaz0";
inline constexpr std::size_t HeaderSize = 0x10;

struct Header {
  std::array<u8, 4> magic;
  u32 decompressed_size;
  // Buffer alignment the decompressed data needs; 0 when the encoder left it unset.
  u32 data_alignment;
  std::array<u8, 4> reserved;
};

struct StreamStats {
  u64 groups = 0;
  u64 literals = 0;
  u64 short_matches = 0;
  u64 long_matches = 0;
  // Matches whose source overlaps the bytes they produce (distance < length).
  u64 overlapping_matches = 0;
  u32 longest_match = 0;
  u32 farthest_distance = 0;
  std::size_t stream_end = 0;
  std::size_t bytes_written = 0;
};

bool probe(Bytes data);
Header read_header(Bytes data);

// Largest decompressed size a stream of `compressed_size` bytes (header included)
// can describe: every group of eight 3-byte matches takes 25 bytes and emits
// 8 * 273 bytes.
u64 max_decompressed_size(std::size_t compressed_size);

// Decodes into `out`, which must already be sized to the header's decompressed
// size. `stats` reflects progress up to the failure point when this throws.
void decompress(Bytes data, std::vector<u8>& out, StreamStats& stats);

void analyze(Bytes data, Analysis& out, const Context& ctx);

}  // namespace nnfck::yaz
