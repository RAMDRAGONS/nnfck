#include "formats/zstd/zstd.hpp"

#include <cstdio>
#include <memory>
#include <vector>

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include <zstd_errors.h>

namespace nnfck::zstd {

namespace {

constexpr u32 FrameMagic = 0xFD2FB528;
constexpr std::size_t MagicSize = 4;
// Unknown content sizes are decoded in steps this large so the budget is
// charged before each allocation rather than after the stream has grown.
constexpr std::size_t GrowStep = std::size_t{1} << 20;

struct Frame {
  std::size_t offset;
  std::size_t compressed_size;
  ZSTD_FrameHeader header;
};

struct DCtxDeleter {
  void operator()(ZSTD_DCtx* d) const { ZSTD_freeDCtx(d); }
};

bool is_skippable(u32 magic) {
  return (magic & ZSTD_MAGIC_SKIPPABLE_MASK) == ZSTD_MAGIC_SKIPPABLE_START;
}

std::string fmt_ratio(u64 part, u64 whole) {
  if (whole == 0)
    return "n/a";
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f%%", static_cast<double>(part) * 100.0 / static_cast<double>(whole));
  return buf;
}

std::string content_size(const ZSTD_FrameHeader& h) {
  return h.frameContentSize == ZSTD_CONTENTSIZE_UNKNOWN ? "Not declared" : fmt_size(h.frameContentSize);
}

std::string dictionary(const ZSTD_FrameHeader& h) {
  return h.dictID == 0 ? "None" : fmt_hex(h.dictID);
}

std::string error_text(std::size_t code) {
  return ZSTD_getErrorName(code);
}

// Splits the input into frames. Bytes after the last complete frame are left
// for the caller to report.
std::vector<Frame> read_frames(Bytes data, Analysis& out, std::size_t& end) {
  std::vector<Frame> frames;
  end = 0;
  while (end < data.size()) {
    Frame f{end, 0, {}};
    const std::size_t rest = data.size() - end;
    const std::size_t hr = ZSTD_getFrameHeader(&f.header, data.data() + end, rest);
    if (ZSTD_isError(hr) || hr != 0) {
      if (frames.empty())
        throw ParseError("invalid frame header: " + (ZSTD_isError(hr) ? error_text(hr) : "truncated"));
      break;
    }
    f.compressed_size = ZSTD_findFrameCompressedSize(data.data() + end, rest);
    if (ZSTD_isError(f.compressed_size)) {
      out.errors.push_back("Zstandard frame " + std::to_string(frames.size()) + " at " + fmt_hex(end) +
                           " is incomplete: " + error_text(f.compressed_size));
      break;
    }
    frames.push_back(f);
    end += f.compressed_size;
  }
  return frames;
}

// Decodes every compressed frame in [0, end). Returns false after recording
// the reason when decoding cannot finish.
bool decompress(Bytes data, std::size_t end, u64 declared, bool sizes_known, std::vector<u8>& payload,
                Analysis& out, const Context& ctx) {
  const std::unique_ptr<ZSTD_DCtx, DCtxDeleter> dctx(ZSTD_createDCtx());
  if (!dctx)
    throw ParseError("cannot allocate a decompression context");

  if (sizes_known) {
    if (!ctx.charge(declared)) {
      out.errors.push_back("analysis budget exhausted before decompressing " + fmt_hex(declared) + " bytes");
      return false;
    }
    payload.reserve(static_cast<std::size_t>(declared));
  }

  ZSTD_inBuffer in{data.data(), end, 0};
  std::size_t produced = 0;
  std::size_t ret = 1;
  while (in.pos < in.size) {
    if (produced == payload.size()) {
      const std::size_t step = sizes_known && declared > produced
                                   ? static_cast<std::size_t>(declared) - produced
                                   : GrowStep;
      if (!sizes_known && !ctx.charge(step)) {
        out.errors.push_back("analysis budget exhausted after decompressing " + fmt_hex(produced) + " bytes");
        return false;
      }
      payload.resize(produced + step);
    }
    ZSTD_outBuffer o{payload.data(), payload.size(), produced};
    ret = ZSTD_decompressStream(dctx.get(), &o, &in);
    produced = o.pos;
    if (ZSTD_isError(ret)) {
      payload.resize(produced);
      if (ZSTD_getErrorCode(ret) == ZSTD_error_dictionary_wrong)
        out.errors.push_back("Zstandard frame needs a dictionary that is not available");
      else
        out.errors.push_back("Zstandard stream is corrupt after " + fmt_hex(produced) + " decoded bytes: " +
                             error_text(ret));
      return false;
    }
    // A frame that declared its size must not produce more.
    if (sizes_known && produced > declared) {
      out.errors.push_back("Zstandard frames produce more than their declared " + fmt_hex(declared) + " bytes");
      return false;
    }
  }
  payload.resize(produced);
  if (ret != 0) {
    out.errors.push_back("Zstandard stream ends mid-frame after " + fmt_hex(produced) + " decoded bytes");
    return false;
  }
  return true;
}

}  // namespace

bool probe(Bytes data) {
  if (data.size() < MagicSize)
    return false;
  const u32 magic = ByteReader(data, Endian::Little).u32_at(0);
  return magic == FrameMagic || is_skippable(magic);
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(Name);
  Section& s = out.section(std::string(Name));
  s.add("Magic", fmt_hex_bytes(data.first(MagicSize)));
  s.add("Compressed Size", fmt_size(data.size()));

  std::size_t end = 0;
  const std::vector<Frame> frames = read_frames(data, out, end);

  std::size_t compressed_frames = 0, skippable_frames = 0;
  u64 declared = 0;
  bool sizes_known = true;
  u32 dict_id = 0;
  for (const Frame& f : frames) {
    if (f.header.frameType == ZSTD_skippableFrame) {
      ++skippable_frames;
      continue;
    }
    ++compressed_frames;
    if (f.header.frameContentSize == ZSTD_CONTENTSIZE_UNKNOWN)
      sizes_known = false;
    else
      declared += f.header.frameContentSize;
    if (dict_id == 0)
      dict_id = f.header.dictID;
  }

  s.add("Frames", std::to_string(compressed_frames));
  if (skippable_frames != 0)
    s.add("Skippable Frames", std::to_string(skippable_frames));
  if (compressed_frames == 1 && skippable_frames == 0) {
    const ZSTD_FrameHeader& h = frames.front().header;
    s.add("Content Size", content_size(h));
    s.add("Window Size", fmt_size(h.windowSize));
    s.add("Dictionary ID", dictionary(h));
    s.add("Content Checksum", fmt_bool(h.checksumFlag != 0));
  } else {
    for (std::size_t i = 0; i < frames.size(); ++i) {
      const Frame& f = frames[i];
      s.add("Frame " + std::to_string(i), "");
      s.add("Offset", fmt_hex(f.offset), 1);
      s.add("Compressed Size", fmt_size(f.compressed_size), 1);
      if (f.header.frameType == ZSTD_skippableFrame) {
        s.add("Type", "Skippable (variant " + std::to_string(f.header.dictID) + ")", 1);
        continue;
      }
      s.add("Content Size", content_size(f.header), 1);
      s.add("Window Size", fmt_size(f.header.windowSize), 1);
      s.add("Dictionary ID", dictionary(f.header), 1);
      s.add("Content Checksum", fmt_bool(f.header.checksumFlag != 0), 1);
    }
  }
  if (end != data.size()) {
    s.add("Trailing Bytes", fmt_size(data.size() - end));
    out.warnings.push_back("Zstandard frames are followed by " + std::to_string(data.size() - end) +
                           " bytes that are not a frame");
  }
  if (!out.errors.empty() || compressed_frames == 0) {
    if (compressed_frames == 0)
      out.warnings.push_back("Zstandard input holds no compressed frames");
    return;
  }

  if (dict_id != 0) {
    out.format = std::string(Name) + " (not decompressed)";
    out.errors.push_back("Zstandard frames need dictionary " + fmt_hex(dict_id) + ", which is not available");
    return;
  }

  std::vector<u8> payload;
  if (!decompress(data, end, declared, sizes_known, payload, out, ctx)) {
    out.format = std::string(Name) + " (corrupt stream)";
    return;
  }
  s.add("Decompressed Size", fmt_size(payload.size()));
  s.add("Compression Ratio", fmt_ratio(data.size(), payload.size()));
  if (sizes_known && payload.size() != declared)
    out.errors.push_back("Zstandard frames declare " + fmt_hex(declared) + " bytes but decode to " +
                         fmt_hex(payload.size()));

  absorb_payload(out, Name, analyze(Bytes(payload), ctx.payload()));
}

}  // namespace nnfck::zstd
