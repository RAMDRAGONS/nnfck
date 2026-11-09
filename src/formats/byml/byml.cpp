#include "formats/byml/byml.hpp"

#include <string>

namespace nnfck::byml {

namespace {

constexpr std::size_t HeaderSize = 0x10;
constexpr u16 MaxKnownVersion = 7;
// Bounds the probe so arbitrary data starting with "BY" or "YB" is not claimed.
constexpr u16 MaxPlausibleVersion = 10;

constexpr u8 NodeArray = 0xC0;
constexpr u8 NodeHash = 0xC1;
constexpr u8 NodeStringTable = 0xC2;
constexpr u8 NodePathTable = 0xC3;

Endian endian_of(Bytes data) {
  return data[0] == 'B' ? Endian::Big : Endian::Little;
}

std::string node_type_name(u8 type) {
  switch (type) {
  case NodeArray: return "Array";
  case NodeHash: return "Hash";
  case NodeStringTable: return "String Table";
  case NodePathTable: return "Path Table";
  default: return "Unknown (" + fmt_hex(type, 2) + ")";
  }
}

// Container nodes pack a one-byte type with a 24-bit entry count stored in the
// document's byte order.
u32 node_count(const ByteReader& r, std::size_t offset) {
  r.require(offset, 4, "BYML node header");
  const Bytes b = r.data().subspan(offset + 1, 3);
  return r.endian() == Endian::Big ? u32(b[0]) << 16 | u32(b[1]) << 8 | b[2]
                                   : u32(b[2]) << 16 | u32(b[1]) << 8 | b[0];
}

void add_table(Section& s, Analysis& out, const ByteReader& r, std::string_view label, u32 offset) {
  if (offset == 0) {
    s.add(std::string(label), "None");
    return;
  }
  const u8 type = r.u8_at(offset);
  if (type != NodeStringTable) {
    out.warnings.push_back("BYML " + std::string(label) + " at " + fmt_hex(offset) + " is a " +
                           node_type_name(type) + " node, not a string table");
    s.add(std::string(label), fmt_hex(offset));
    return;
  }
  s.add(std::string(label), fmt_hex(offset) + " (" + std::to_string(node_count(r, offset)) + " entries)");
}

}  // namespace

bool probe(Bytes data) {
  if (data.size() < HeaderSize)
    return false;
  const ByteReader r(data, Endian::Big);
  if (!r.magic_at(0, "BY") && !r.magic_at(0, "YB"))
    return false;
  const u16 version = ByteReader(data, endian_of(data)).u16_at(2);
  return version >= 1 && version <= MaxPlausibleVersion;
}

void analyze(Bytes data, Analysis& out, const Context&) {
  out.format = std::string(Name);
  const ByteReader r(data, endian_of(data));
  r.require(0, HeaderSize, "BYML header");

  const u16 version = r.u16_at(0x2);
  const u32 hash_keys = r.u32_at(0x4);
  const u32 strings = r.u32_at(0x8);
  u32 root = r.u32_at(0xC);

  Section& s = out.section(std::string(Name));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Version", std::to_string(version));
  add_table(s, out, r, "Hash Key Table", hash_keys);
  add_table(s, out, r, "String Table", strings);

  // Some early documents insert a path table offset before the root offset.
  if (root != 0 && r.u8_at(root) == NodePathTable) {
    s.add("Path Table", fmt_hex(root) + " (" + std::to_string(node_count(r, root)) + " entries)");
    root = r.u32_at(0x10);
  }

  if (root == 0) {
    s.add("Root Node", "None (empty document)");
  } else {
    const u8 type = r.u8_at(root);
    std::string desc = fmt_hex(root) + ", " + node_type_name(type);
    if (type == NodeArray || type == NodeHash)
      desc += " with " + std::to_string(node_count(r, root)) + " entries";
    else
      out.warnings.push_back("BYML root node is a " + node_type_name(type) + " node, not an array or hash");
    s.add("Root Node", desc);
  }

  if (version > MaxKnownVersion)
    out.warnings.push_back("BYML version " + std::to_string(version) + " is newer than the known " +
                           std::to_string(MaxKnownVersion));
}

}  // namespace nnfck::byml
