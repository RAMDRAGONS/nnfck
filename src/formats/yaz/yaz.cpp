#include "formats/yaz/yaz.hpp"

#include <algorithm>
#include <cstdio>

namespace nnfck::yaz {

namespace {

constexpr u32 MaxShortMatch = 0x11;
constexpr u32 MaxLongMatch = 0xFF + 0x12;
constexpr std::size_t ChunksPerGroup = 8;
constexpr std::size_t MaxGroupBytes = 1 + ChunksPerGroup * 3;

std::string fmt_ratio(u64 part, u64 whole) {
  if (whole == 0)
    return "n/a";
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f%%", static_cast<double>(part) * 100.0 / static_cast<double>(whole));
  return buf;
}

bool is_power_of_two(u32 v) {
  return v != 0 && (v & (v - 1)) == 0;
}

void add_header_fields(Section& s, const Header& h, std::size_t compressed_size) {
  s.add("Magic", fmt_quoted(h.magic));
  s.add("Compressed Size", fmt_size(compressed_size));
  s.add("Decompressed Size", fmt_size(h.decompressed_size));
  s.add("Compression Ratio", fmt_ratio(compressed_size, h.decompressed_size));
  s.add("Data Alignment", h.data_alignment == 0 ? "None (0)" : fmt_hex(h.data_alignment));
  s.add("Reserved", fmt_hex_bytes(h.reserved));
}

void add_stream_fields(Section& s, const StreamStats& st, std::size_t compressed_size) {
  s.add("Stream", "");
  s.add("Groups", std::to_string(st.groups), 1);
  s.add("Literals", std::to_string(st.literals), 1);
  s.add("Back-references", std::to_string(st.short_matches + st.long_matches), 1);
  s.add("2-byte", std::to_string(st.short_matches), 2);
  s.add("3-byte", std::to_string(st.long_matches), 2);
  s.add("Overlapping", std::to_string(st.overlapping_matches), 2);
  if (st.short_matches + st.long_matches != 0) {
    s.add("Longest Match", std::to_string(st.longest_match) + " bytes", 1);
    s.add("Farthest Distance", fmt_hex(st.farthest_distance), 1);
  }
  s.add("Stream End", fmt_hex(st.stream_end), 1);
  s.add("Trailing Bytes", fmt_size(compressed_size - st.stream_end), 1);
}

}  // namespace

bool probe(Bytes data) {
  return ByteReader(data, Endian::Big).magic_at(0, Magic);
}

Header read_header(Bytes data) {
  const ByteReader r(data, Endian::Big);
  r.require(0, HeaderSize, "Sead Yaz header");
  Header h{};
  std::copy_n(data.begin(), 4, h.magic.begin());
  h.decompressed_size = r.u32_at(0x4);
  h.data_alignment = r.u32_at(0x8);
  std::copy_n(data.begin() + 0xC, 4, h.reserved.begin());
  return h;
}

u64 max_decompressed_size(std::size_t compressed_size) {
  if (compressed_size <= HeaderSize)
    return 0;
  const u64 body = compressed_size - HeaderSize;
  return (body + MaxGroupBytes - 1) / MaxGroupBytes * ChunksPerGroup * MaxLongMatch;
}

void decompress(Bytes data, std::vector<u8>& out, StreamStats& st) {
  const std::size_t size = data.size();
  const std::size_t dst_size = out.size();
  std::size_t src = HeaderSize;
  std::size_t dst = 0;
  u8 group = 0;
  std::size_t remaining = 0;

  // Write the cursors back on every exit path so partial progress is reported.
  struct Sync {
    StreamStats& st;
    const std::size_t& src;
    const std::size_t& dst;
    ~Sync() {
      st.stream_end = src;
      st.bytes_written = dst;
    }
  } sync{st, src, dst};

  const auto truncated = [&](std::string_view what) {
    return ParseError("stream truncated reading " + std::string(what) + " at offset " + fmt_hex(src) + " (" +
                      fmt_hex(dst) + " of " + fmt_hex(dst_size) + " bytes decoded)");
  };

  while (dst < dst_size) {
    if (remaining == 0) {
      if (src >= size)
        throw truncated("group header");
      group = data[src++];
      remaining = ChunksPerGroup;
      ++st.groups;
    }

    if (group & 0x80) {
      if (src >= size)
        throw truncated("literal");
      out[dst++] = data[src++];
      ++st.literals;
    } else {
      const std::size_t chunk_offset = src;
      if (size - src < 2)
        throw truncated("back-reference");
      const u8 b0 = data[src], b1 = data[src + 1];
      src += 2;
      const u32 distance = (u32(b0 & 0x0F) << 8 | b1) + 1;
      u32 length = b0 >> 4;
      if (length == 0) {
        if (src >= size)
          throw truncated("extended length");
        length = u32(data[src++]) + MaxShortMatch + 1;
        ++st.long_matches;
      } else {
        length += 2;
        ++st.short_matches;
      }

      if (distance > dst)
        throw ParseError("back-reference at offset " + fmt_hex(chunk_offset) + " reaches " +
                         std::to_string(distance) + " bytes back but only " + std::to_string(dst) +
                         " bytes have been decoded");
      if (length > dst_size - dst)
        throw ParseError("back-reference at offset " + fmt_hex(chunk_offset) + " copies " + std::to_string(length) +
                         " bytes, overrunning the decompressed size by " +
                         std::to_string(length - (dst_size - dst)));

      if (distance < length)
        ++st.overlapping_matches;
      st.longest_match = std::max(st.longest_match, length);
      st.farthest_distance = std::max(st.farthest_distance, distance);

      // Byte-wise copy is required: overlapping matches replicate bytes produced
      // earlier in this same copy.
      for (std::size_t i = 0; i < length; ++i, ++dst)
        out[dst] = out[dst - distance];
    }

    group = u8(group << 1);
    --remaining;
  }
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  const Header h = read_header(data);
  Section& s = out.section(std::string(Name));
  add_header_fields(s, h, data.size());

  if (h.data_alignment != 0 && !is_power_of_two(h.data_alignment))
    out.warnings.push_back("Sead Yaz data alignment " + fmt_hex(h.data_alignment) + " is not a power of two");
  if (std::any_of(h.reserved.begin(), h.reserved.end(), [](u8 b) { return b != 0; }))
    out.warnings.push_back("Sead Yaz reserved bytes are not zero");

  const u64 limit = max_decompressed_size(data.size());
  if (h.decompressed_size > limit) {
    out.format = std::string(Name) + " (corrupt)";
    out.errors.push_back("Sead Yaz decompressed size " + fmt_hex(h.decompressed_size) + " is impossible for " +
                         fmt_hex(data.size()) + " bytes of input (at most " + fmt_hex(limit) + ")");
    return;
  }

  if (!ctx.charge(h.decompressed_size)) {
    out.format = std::string(Name) + " (not decompressed)";
    out.errors.push_back("analysis budget exhausted before decompressing " + fmt_hex(h.decompressed_size) +
                         " bytes");
    return;
  }

  std::vector<u8> payload(h.decompressed_size);
  StreamStats st;
  try {
    decompress(data, payload, st);
  } catch (const ParseError& e) {
    add_stream_fields(s, st, data.size());
    out.format = std::string(Name) + " (corrupt stream)";
    out.errors.push_back(std::string("Sead Yaz: ") + e.what());
    return;
  }
  add_stream_fields(s, st, data.size());

  const std::size_t trailing = data.size() - st.stream_end;
  if (trailing != 0) {
    const Bytes tail = data.subspan(st.stream_end);
    if (std::any_of(tail.begin(), tail.end(), [](u8 b) { return b != 0; }))
      out.warnings.push_back("Sead Yaz stream is followed by " + std::to_string(trailing) +
                             " non-zero trailing bytes");
  }

  Analysis inner = analyze(Bytes(payload), ctx.nested());
  if (inner.format == "Unknown")
    out.format = "Sead Yaz-compressed unknown data";
  else if (inner.format == "Empty")
    out.format = "Sead Yaz-compressed empty data";
  else
    out.format = "Sead Yaz-compressed " + inner.format;
  out.absorb(std::move(inner));
}

}  // namespace nnfck::yaz
