#include "formats/pmaa/pmaa.hpp"

#include <algorithm>

#include "core/crc32.hpp"
#include "formats/pmaa/common.hpp"
#include "formats/pmaa/names.hpp"

namespace nnfck::pmaa {

namespace {

// The PMAA signature as a little-endian u32.
constexpr std::string_view Magic = "AAMP";
constexpr std::size_t MaxTypeLength = 0x40;

constexpr std::size_t HeaderSize1 = 0x18;
constexpr std::size_t NodeHeaderSize1 = 0x10;
constexpr std::size_t ParamHeaderSize1 = 0xC;

constexpr std::size_t HeaderSize2 = 0x30;
constexpr std::size_t ListSize2 = 0xC;
constexpr std::size_t ObjectSize2 = 0x8;
constexpr std::size_t ParamSize2 = 0x8;
constexpr u32 CurveSize = 0x80;

constexpr u32 LittleEndianFlag = 1u << 0;
constexpr u32 Utf8Flag = 1u << 1;

// Bytes the value of a fixed-size type occupies.
u32 fixed_size(u32 type) {
  if (const std::size_t n = float_components(type))
    return static_cast<u32>(n * 4);
  if (is_curve(type))
    return CurveSize * (type - Curve1Type + 1);
  return 4;
}

class Walker {
public:
  Walker(const ByteReader& r, Analysis& out, const Context& ctx) : contents(out), m_r(r), m_ctx(ctx) {}

  Contents contents;

  bool note_param(u32 hash, u32 type, std::size_t at) {
    contents.param(type);
    if (type >= ParamTypeCount) {
      contents.problem("Parameter " + display_name(hash) + " at " + fmt_hex(at) + " has unknown type " +
                       std::to_string(type));
      return false;
    }
    return true;
  }

  // Version 2: lists hold offsets, in 4-byte units from themselves, to arrays
  // of child lists and objects; objects likewise to their parameters, and
  // parameters to their values.
  void list2(std::size_t at, int depth) {
    if (depth > MaxDepth)
      throw ParseError("parameter lists nest deeper than " + std::to_string(MaxDepth) + " levels");
    m_ctx.spend(ListSize2, "parameter lists");
    m_r.require(at, ListSize2, "parameter list");
    const u32 hash = m_r.u32_at(at);
    const u32 l = m_r.u32_at(at + 4);
    const u32 o = m_r.u32_at(at + 8);
    const std::size_t list_at = at + 4 * std::size_t{l & 0xFFFF};
    const std::size_t obj_at = at + 4 * std::size_t{o & 0xFFFF};
    const u32 list_count = l >> 16;
    const u32 obj_count = o >> 16;
    contents.list(display_name(hash), depth, list_count, obj_count);
    for (u32 i = 0; i < list_count; ++i)
      list2(list_at + std::size_t{i} * ListSize2, depth + 1);
    for (u32 i = 0; i < obj_count; ++i)
      object2(obj_at + std::size_t{i} * ObjectSize2, depth + 1);
  }

  void object2(std::size_t at, int depth) {
    m_ctx.spend(ObjectSize2, "parameter objects");
    m_r.require(at, ObjectSize2, "parameter object");
    const u32 hash = m_r.u32_at(at);
    const u32 p = m_r.u32_at(at + 4);
    const std::size_t param_at = at + 4 * std::size_t{p & 0xFFFF};
    const u32 count = p >> 16;
    contents.object(display_name(hash), depth, count);
    for (u32 i = 0; i < count; ++i)
      param2(param_at + std::size_t{i} * ParamSize2);
  }

  void param2(std::size_t at) {
    m_ctx.spend(ParamSize2, "parameters");
    m_r.require(at, ParamSize2, "parameter");
    const u32 hash = m_r.u32_at(at);
    const u32 v = m_r.u32_at(at + 4);
    const u32 type = v >> 24;
    if (!note_param(hash, type, at))
      return;
    const u64 data = u64{at} + 4 * u64{v & 0xFFFFFF};
    u64 size = fixed_size(type);
    if (is_string(type)) {
      if (data >= m_r.size()) {
        size = 1;
      } else {
        const Bytes rest = m_r.data().subspan(static_cast<std::size_t>(data));
        const auto nul = std::find(rest.begin(), rest.end(), u8{0});
        size = nul == rest.end() ? rest.size() + 1 : static_cast<u64>(nul - rest.begin()) + 1;
      }
    } else if (is_buffer(type)) {
      if (data < 4 || data > m_r.size()) {
        size = 1;
      } else {
        const u32 count = m_r.u32_at(static_cast<std::size_t>(data) - 4);
        size = type == BinaryBufferType ? u64{count} : u64{count} * 4;
      }
    }
    if (data > m_r.size() || size > m_r.size() - data)
      contents.problem("Parameter " + display_name(hash) + " at " + fmt_hex(at) + " places its " +
              std::string(ParamTypes[type]) + " value at " + fmt_hex(data) + " past the end of the file");
  }

  // Version 1: every node opens with its size, and a list's child lists then
  // its objects follow its header back to back. Returns the node's size.
  u32 list1(std::size_t at, std::size_t end, int depth) {
    if (depth > MaxDepth)
      throw ParseError("parameter lists nest deeper than " + std::to_string(MaxDepth) + " levels");
    m_ctx.spend(NodeHeaderSize1, "parameter lists");
    const u32 size = node_size(at, end, NodeHeaderSize1, "parameter list");
    const u32 hash = m_r.u32_at(at + 4);
    const u32 list_count = m_r.u32_at(at + 8);
    const u32 obj_count = m_r.u32_at(at + 0xC);
    contents.list(display_name(hash), depth, list_count, obj_count);
    const std::size_t node_end = at + size;
    std::size_t p = at + NodeHeaderSize1;
    for (u32 i = 0; i < list_count; ++i)
      p += list1(p, node_end, depth + 1);
    for (u32 i = 0; i < obj_count; ++i)
      p += object1(p, node_end, depth + 1);
    if (p != node_end)
      contents.problem("Parameter list " + display_name(hash) + " at " + fmt_hex(at) + " spans " + fmt_hex(size) +
              " bytes but its children end at " + fmt_hex(p - at));
    return size;
  }

  u32 object1(std::size_t at, std::size_t end, int depth) {
    m_ctx.spend(NodeHeaderSize1, "parameter objects");
    const u32 size = node_size(at, end, NodeHeaderSize1, "parameter object");
    const u32 count = m_r.u32_at(at + 4);
    const u32 hash = m_r.u32_at(at + 8);
    contents.object(display_name(hash), depth, count);
    const std::size_t node_end = at + size;
    std::size_t p = at + NodeHeaderSize1;
    for (u32 i = 0; i < count; ++i) {
      m_ctx.spend(ParamHeaderSize1, "parameters");
      const u32 param_size = node_size(p, node_end, ParamHeaderSize1, "parameter");
      const u32 type = m_r.u32_at(p + 4);
      const u32 param_hash = m_r.u32_at(p + 8);
      // Version 1 stores a bool in a single byte.
      const u32 needed = type == BoolType ? 1 : fixed_size(type);
      if (note_param(param_hash, type, p) && !is_string(type) && !is_buffer(type) &&
          param_size - ParamHeaderSize1 < needed)
        contents.problem("Parameter " + display_name(param_hash) + " at " + fmt_hex(p) + " holds " +
                fmt_hex(param_size - ParamHeaderSize1) + " bytes, too few for a " + std::string(ParamTypes[type]));
      p += param_size;
    }
    if (p != node_end)
      contents.problem("Parameter object " + display_name(hash) + " at " + fmt_hex(at) + " spans " + fmt_hex(size) +
              " bytes but its parameters end at " + fmt_hex(p - at));
    return size;
  }

private:
  const ByteReader& m_r;
  const Context& m_ctx;

  u32 node_size(std::size_t at, std::size_t end, std::size_t header, std::string_view what) const {
    if (at > end || end - at < header)
      throw ParseError(std::string(what) + " at " + fmt_hex(at) + " runs past its parent");
    const u32 size = m_r.u32_at(at);
    if (size < header || size > end - at)
      throw ParseError(std::string(what) + " at " + fmt_hex(at) + " claims " + fmt_hex(size) + " bytes where " +
                       fmt_hex(end - at) + " remain");
    return size;
  }
};

void check_file_size(Analysis& out, u32 declared, std::size_t actual) {
  if (declared != actual)
    out.warnings.push_back("Parameter archive declares " + fmt_hex(declared) + " bytes but holds " +
                           fmt_hex(actual));
}

void check_root(Analysis& out, const ByteReader& r, std::size_t hash_at) {
  if (r.has(hash_at, 4) && r.u32_at(hash_at) != crc32(RootName))
    out.warnings.push_back("Parameter archive root is " + display_name(r.u32_at(hash_at)) + " rather than " +
                           std::string(RootName));
}

void finish(Walker& w, Analysis& out, Section& s, const Context& ctx) {
  if (const std::string& problem = names_problem(); !problem.empty())
    out.warnings.push_back("Parameter archive names are shown as hashes: " + problem);
  w.contents.finish(s, ctx);
}

void analyze_v1(ByteReader& r, Analysis& out, Section& s, const Context& ctx) {
  r.require(0, HeaderSize1, "parameter archive header");
  const u32 file_size = r.u32_at(0xC);
  const u32 pio_version = r.u32_at(0x10);
  const u32 type_length = r.u32_at(0x14);
  if (type_length > MaxTypeLength || !r.has(HeaderSize1, type_length))
    throw ParseError("type string claims " + fmt_hex(type_length) + " bytes");
  const std::string type = r.cstring_at(HeaderSize1, type_length);
  s.add("File Size", fmt_size(file_size));
  s.add("Extension", extension_label(type, true));
  s.add("Data Version", std::to_string(pio_version));
  check_file_size(out, file_size, r.size());

  const std::size_t root = HeaderSize1 + type_length;
  check_root(out, r, root + 4);
  Walker w(r, out, ctx);
  w.list1(root, r.size(), 0);
  s.add("Lists", std::to_string(w.contents.lists));
  s.add("Objects", std::to_string(w.contents.objects));
  s.add("Parameters", std::to_string(w.contents.params));
  finish(w, out, s, ctx);
}

// Header counts and section sizes describe one table of lists, one of
// objects and one of parameters after the root, then parameter values,
// strings and a final section of size-prefixed chunks.
void analyze_v2(ByteReader& r, Analysis& out, Section& s, const Context& ctx) {
  r.require(0, HeaderSize2, "parameter archive header");
  const u32 flags = r.u32_at(0x8);
  const u32 file_size = r.u32_at(0xC);
  const u32 pio_version = r.u32_at(0x10);
  const u32 pio_offset = r.u32_at(0x14);
  const u32 list_count = r.u32_at(0x18);
  const u32 obj_count = r.u32_at(0x1C);
  const u32 param_count = r.u32_at(0x20);
  const u32 data_size = r.u32_at(0x24);
  const u32 string_size = r.u32_at(0x28);
  const u32 unknown_size = r.u32_at(0x2C);
  const std::string type =
      pio_offset == 0 ? std::string() : r.cstring_at(HeaderSize2, std::min<std::size_t>(pio_offset, MaxTypeLength));

  s.add("String Encoding", flags & Utf8Flag ? "UTF-8" : "Shift-JIS");
  s.add("File Size", fmt_size(file_size));
  s.add("Extension", extension_label(type, true));
  s.add("Data Version", std::to_string(pio_version));
  s.add("Lists", std::to_string(list_count));
  s.add("Objects", std::to_string(obj_count));
  s.add("Parameters", std::to_string(param_count));
  s.add("Data Section", fmt_size(data_size));
  s.add("String Section", fmt_size(string_size));
  s.add("Unknown Section", fmt_size(unknown_size));
  check_file_size(out, file_size, r.size());

  const u64 root = HeaderSize2 + u64{pio_offset};
  const u64 laid_out = root + u64{list_count} * ListSize2 + u64{obj_count} * ObjectSize2 +
                       u64{param_count} * ParamSize2 + data_size + string_size + unknown_size;
  if (laid_out != file_size)
    out.warnings.push_back("Parameter archive sections end at " + fmt_hex(laid_out) + " but the file size is " +
                           fmt_hex(file_size));
  if (root > r.size())
    throw ParseError("root list at " + fmt_hex(root) + " lies past the end of the file");

  check_root(out, r, static_cast<std::size_t>(root));
  Walker w(r, out, ctx);
  w.list2(static_cast<std::size_t>(root), 0);
  const auto compare = [&](u64 declared, u64 found, std::string_view what) {
    if (declared != found)
      out.warnings.push_back("Parameter archive header counts " + std::to_string(declared) + " " +
                             std::string(what) + " but its tree holds " + std::to_string(found));
  };
  compare(list_count, w.contents.lists, "lists");
  compare(obj_count, w.contents.objects, "objects");
  compare(param_count, w.contents.params, "parameters");
  finish(w, out, s, ctx);
}

}  // namespace

bool probe(Bytes data) {
  return data.size() >= 8 && ByteReader(data, Endian::Little).magic_at(0, Magic);
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  ByteReader r(data, Endian::Little);
  r.require(0, 0xC, "parameter archive header");
  // The flags' low byte comes first only when the little-endian flag is
  // stored in little-endian order.
  if ((r.u32_at(0x8) & LittleEndianFlag) == 0)
    r.set_endian(Endian::Big);
  const u32 v = r.u32_at(0x4);

  out.format = std::string(Name);
  Section& s = out.section(std::string(Name));
  s.add("Version", std::to_string(v));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  if (v == 1)
    analyze_v1(r, out, s, ctx);
  else if (v == 2)
    analyze_v2(r, out, s, ctx);
  else
    out.warnings.push_back("Parameter archive version " + std::to_string(v) +
                           " has an unknown layout; only its header is shown");
}

}  // namespace nnfck::pmaa
