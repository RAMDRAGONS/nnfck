#include "formats/bfres/bfres.hpp"

#include <string>

namespace nnfck::bfres {

namespace {

constexpr std::size_t MinSize = 0x20;
constexpr std::size_t MaxNameLength = 0x400;

Endian read_bom(const ByteReader& r, std::size_t offset) {
  const u8 a = r.u8_at(offset), b = r.u8_at(offset + 1);
  if (a == 0xFE && b == 0xFF)
    return Endian::Big;
  if (a == 0xFF && b == 0xFE)
    return Endian::Little;
  throw ParseError("invalid byte order mark " + fmt_hex_bytes(r.data().subspan(offset, 2)));
}

void check_file_size(Analysis& out, u32 declared, std::size_t actual) {
  if (declared != actual)
    out.warnings.push_back("BFRES declares " + fmt_hex(declared) + " bytes but holds " + fmt_hex(actual));
}

// Wii U (nw::g3d): four version bytes major.minor.micro.binaryBugfix, then a
// big-endian header with self-relative 32-bit offsets.
void analyze_cafe(ByteReader& r, Analysis& out) {
  const Bytes v = r.data().subspan(0x4, 4);
  r.set_endian(read_bom(r, 0x8));
  const u16 header_size = r.u16_at(0xA);
  const u32 file_size = r.u32_at(0xC);
  const u32 alignment = r.u32_at(0x10);
  const s32 name_rel = r.s32_at(0x14);

  out.format = std::string(Name) + " (Wii U)";
  Section& s = out.section(std::string(Name));
  s.add("Platform", "Wii U");
  s.add("Version", std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]) + "." +
                       std::to_string(v[3]));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Header Size", fmt_hex(header_size));
  s.add("File Size", fmt_size(file_size));
  s.add("Alignment", fmt_hex(alignment));
  if (name_rel != 0) {
    const std::size_t at = static_cast<std::size_t>(0x14 + static_cast<std::int64_t>(name_rel));
    s.add("Name", r.cstring_at(at, MaxNameLength));
  }

  if (v[0] < 3 || v[0] > 4)
    out.warnings.push_back("Wii U BFRES version " + std::to_string(v[0]) + ".x is outside the known 3.x to 4.x range");
  check_file_size(out, file_size, r.size());
}

// Switch (nn::util::BinaryFileHeader): an 8-byte "FRES    " signature, version
// as u8 micro, u8 minor, u16 major, and absolute 64-bit offsets.
void analyze_nx(ByteReader& r, Analysis& out) {
  r.set_endian(read_bom(r, 0xC));
  const u32 version = r.u32_at(0x8);
  const u16 major = static_cast<u16>(version >> 16);
  const u8 minor = static_cast<u8>(version >> 8);
  const u8 micro = static_cast<u8>(version);
  const u8 alignment_shift = r.u8_at(0xE);
  const u8 address_size = r.u8_at(0xF);
  const u16 relocated = r.u16_at(0x14);
  const u16 first_block = r.u16_at(0x16);
  const u32 relocation_table = r.u32_at(0x18);
  const u32 file_size = r.u32_at(0x1C);

  out.format = std::string(Name) + " (Switch)";
  Section& s = out.section(std::string(Name));
  s.add("Platform", "Switch");
  s.add("Version", std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(micro));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Alignment", alignment_shift < 32 ? fmt_hex(u64{1} << alignment_shift) : "Invalid (2^" +
                                                std::to_string(alignment_shift) + ")");
  s.add("Target Address Size", std::to_string(address_size));
  s.add("First Block Offset", fmt_hex(first_block));
  s.add("Relocation Table", fmt_hex(relocation_table));
  s.add("Relocated", fmt_bool(relocated != 0));
  s.add("File Size", fmt_size(file_size));

  const u64 name_ptr = r.u64_at(0x20);
  if (name_ptr != 0 && r.has(name_ptr, 2)) {
    const u16 length = r.u16_at(static_cast<std::size_t>(name_ptr));
    r.require(static_cast<std::size_t>(name_ptr) + 2, length, "BFRES name");
    const Bytes name = r.data().subspan(static_cast<std::size_t>(name_ptr) + 2, length);
    s.add("Name", std::string(name.begin(), name.end()));
  }

  if (relocation_table != 0 && !r.magic_at(relocation_table, "_RLT"))
    out.warnings.push_back("BFRES relocation table at " + fmt_hex(relocation_table) + " lacks its _RLT signature");

  if (major > 10 || major == 1 || major == 6 || major == 7)
    out.warnings.push_back("Switch BFRES version " + std::to_string(major) + ".x has no verified layout");

  // From 10.x a GPU buffer section may follow the declared file image, so only
  // a declared size beyond the data is a definite inconsistency.
  if (major >= 10) {
    if (file_size > r.size())
      out.warnings.push_back("BFRES declares " + fmt_hex(file_size) + " bytes but holds only " +
                             fmt_hex(r.size()));
  } else {
    check_file_size(out, file_size, r.size());
  }
}

}  // namespace

bool probe(Bytes data) {
  return ByteReader(data, Endian::Big).magic_at(0, "FRES");
}

void analyze(Bytes data, Analysis& out, const Context&) {
  out.format = std::string(Name);
  ByteReader r(data, Endian::Big);
  r.require(0, MinSize, "BFRES header");
  if (r.magic_at(4, "    "))
    analyze_nx(r, out);
  else
    analyze_cafe(r, out);
}

}  // namespace nnfck::bfres
