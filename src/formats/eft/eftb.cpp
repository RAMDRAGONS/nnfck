#include "formats/eft/eftb.hpp"

#include <string>
#include <vector>

namespace nnfck::eft {

namespace {

constexpr std::size_t EftbHeaderSize = 0x30;
constexpr std::size_t VfxbHeaderSize = 0x20;
constexpr std::size_t NameLength = 0x20;
constexpr std::size_t MaxNameLength = 0x100;
constexpr std::size_t NodeHeaderSize = 0x20;
constexpr std::size_t EsetEmitterCountOffset = 0x50;
constexpr u32 NoOffset = 0xFFFFFFFF;
// Upper bound on nodes visited along one sibling chain, so a corrupt chain
// that loops back on itself terminates.
constexpr std::size_t MaxChainLength = 0x100000;

struct Node {
  std::size_t offset;
  std::string magic;
  u32 child;
  u32 sibling;
  u32 data;
  u16 child_count;
};

Node read_node(const ByteReader& r, std::size_t offset) {
  r.require(offset, NodeHeaderSize, "effect node header");
  return Node{offset,
              std::string(reinterpret_cast<const char*>(r.data().data() + offset), 4),
              r.u32_at(offset + 0x08),
              r.u32_at(offset + 0x0C),
              r.u32_at(offset + 0x14),
              r.u16_at(offset + 0x1C)};
}

// Child and sibling offsets are relative to the node that holds them.
std::vector<Node> read_chain(const ByteReader& r, std::size_t first) {
  std::vector<Node> nodes;
  std::size_t offset = first;
  while (true) {
    if (nodes.size() >= MaxChainLength)
      throw ParseError("effect node chain starting at " + fmt_hex(first) + " does not terminate");
    nodes.push_back(read_node(r, offset));
    const u32 sibling = nodes.back().sibling;
    if (sibling == NoOffset)
      break;
    if (sibling == 0)
      throw ParseError("effect node at " + fmt_hex(offset) + " names itself as its sibling");
    offset += sibling;
  }
  return nodes;
}

std::string fixed_string(const ByteReader& r, std::size_t offset, std::size_t length) {
  r.require(offset, length, "string");
  std::string s;
  for (std::size_t i = 0; i < length; ++i) {
    const u8 c = r.u8_at(offset + i);
    if (c == 0)
      break;
    s.push_back(static_cast<char>(c));
  }
  return s;
}

std::string display_magic(const std::string& magic) {
  for (const char c : magic)
    if (c < 0x20 || c >= 0x7F)
      return fmt_quoted(Bytes(reinterpret_cast<const u8*>(magic.data()), magic.size()));
  return magic;
}

void add_nodes(Section& s, Analysis& out, const ByteReader& r, std::size_t first) {
  const std::vector<Node> roots = read_chain(r, first);
  s.add("Root Nodes", std::to_string(roots.size()));
  for (const Node& n : roots)
    s.add(display_magic(n.magic), fmt_hex(n.offset) + ", " + std::to_string(n.child_count) + (n.child_count == 1 ? " child" : " children"), 1);

  for (const Node& esta : roots) {
    if (esta.magic != "ESTA")
      continue;
    // Only ESET children are emitter sets; other node types may share the chain.
    std::size_t linked = 0, sets = 0;
    u64 emitters = 0;
    if (esta.child != NoOffset) {
      for (const Node& child : read_chain(r, esta.offset + esta.child)) {
        ++linked;
        if (child.magic != "ESET")
          continue;
        ++sets;
        emitters += r.u32_at(child.offset + child.data + EsetEmitterCountOffset);
      }
    }
    if (linked != esta.child_count)
      out.warnings.push_back("ESTA declares " + std::to_string(esta.child_count) + " children but links " +
                             std::to_string(linked));
    s.add("Emitter Sets", std::to_string(sets));
    s.add("Emitters", std::to_string(emitters));
  }
}

std::string endian_mode(u8 v) {
  switch (v) {
  case 0: return "Little endian";
  case 1: return "Big endian";
  default: return "Unknown (" + std::to_string(v) + ")";
  }
}

// EFTB: a 0x30-byte header whose tag and version are stored in the CPU byte
// order the header itself records at 0x28.
void analyze_eftb(Bytes data, Analysis& out) {
  ByteReader r(data, Endian::Big);
  r.require(0, EftbHeaderSize, "EFTB header");
  const u8 cpu_endian = r.u8_at(0x28);
  const u8 gpu_endian = r.u8_at(0x29);
  const u8 target_api = r.u8_at(0x2A);
  const u8 address_size = r.u8_at(0x2B);
  if (cpu_endian > 1)
    throw ParseError("invalid CPU endianness " + std::to_string(cpu_endian));
  r.set_endian(cpu_endian == 1 ? Endian::Big : Endian::Little);

  // Of the platforms that use EFTB (Windows, 3DS, Wii U) only Wii U has a
  // big-endian CPU.
  const bool cafe = cpu_endian == 1;
  out.format = std::string(Name) + " (" + std::string(Abbreviation) + (cafe ? ", Wii U)" : ")");

  Section& s = out.section(std::string(Name) + " (" + std::string(Abbreviation) + ")");
  s.add("Signature", "EFTB");
  if (cafe)
    s.add("Platform", "Wii U");
  s.add("Binary Version", std::to_string(r.u32_at(0x04)));
  s.add("Name", fixed_string(r, 0x08, NameLength));
  s.add("CPU Byte Order", endian_mode(cpu_endian));
  s.add("GPU Byte Order", endian_mode(gpu_endian));
  s.add("Target API", std::to_string(target_api));
  s.add("Target Address Size", std::to_string(address_size) + " bytes");
  s.add("Extra Tag", fmt_hex_bytes(data.subspan(0x2C, 4)));
  add_nodes(s, out, r, EftbHeaderSize);
}

// VFXB: nn::util::BinaryFileHeader. Its version field holds the binary
// version as major and the graphics API as minor.
void analyze_vfxb(Bytes data, Analysis& out) {
  ByteReader r(data, Endian::Little);
  r.require(0, VfxbHeaderSize, "VFXB header");
  const u8 b0 = r.u8_at(0xC), b1 = r.u8_at(0xD);
  if (b0 == 0xFE && b1 == 0xFF)
    r.set_endian(Endian::Big);
  else if (!(b0 == 0xFF && b1 == 0xFE))
    throw ParseError("invalid byte order mark " + fmt_hex_bytes(data.subspan(0xC, 2)));
  out.format = std::string(Name) + " (" + std::string(Abbreviation) + ", Switch)";

  const u8 micro = r.u8_at(0x08);
  const u8 graphics_api = r.u8_at(0x09);
  const u16 binary_version = r.u16_at(0x0A);
  const u8 alignment_shift = r.u8_at(0x0E);
  const u8 address_size = r.u8_at(0x0F);
  const u32 name_offset = r.u32_at(0x10);
  const u16 flags = r.u16_at(0x14);
  const u16 first_block = r.u16_at(0x16);
  const u32 relocation_table = r.u32_at(0x18);
  const u32 file_size = r.u32_at(0x1C);

  Section& s = out.section(std::string(Name) + " (" + std::string(Abbreviation) + ")");
  s.add("Signature", "VFXB");
  s.add("Platform", "Switch");
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Binary Version", std::to_string(binary_version));
  s.add("Graphics API", std::to_string(graphics_api));
  s.add("Micro Version", std::to_string(micro));
  s.add("Alignment", alignment_shift < 32 ? fmt_hex(u64{1} << alignment_shift)
                                          : "Invalid (2^" + std::to_string(alignment_shift) + ")");
  s.add("Target Address Size", std::to_string(address_size));
  s.add("Flags", fmt_hex(flags, 4));
  s.add("First Block Offset", fmt_hex(first_block));
  s.add("Relocation Table", relocation_table == 0 ? "None" : fmt_hex(relocation_table));
  s.add("File Size", fmt_size(file_size));
  if (name_offset != 0)
    s.add("Name", r.cstring_at(name_offset, MaxNameLength));
  add_nodes(s, out, r, first_block);

  if (file_size != data.size())
    out.warnings.push_back("VFXB declares " + fmt_hex(file_size) + " bytes but holds " + fmt_hex(data.size()));
}

}  // namespace

bool probe(Bytes data) {
  const ByteReader r(data, Endian::Big);
  return r.magic_at(0, "EFTB") || r.magic_at(0, "VFXB");
}

void analyze(Bytes data, Analysis& out, const Context&) {
  if (ByteReader(data, Endian::Big).magic_at(0, "VFXB"))
    analyze_vfxb(data, out);
  else
    analyze_eftb(data, out);
}

}  // namespace nnfck::eft
