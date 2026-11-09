#include "formats/sarc/sarc.hpp"

#include <string>
#include <vector>

namespace nnfck::sarc {

namespace {

constexpr std::size_t HeaderSize = 0x14;
constexpr std::size_t FatHeaderSize = 0xC;
constexpr std::size_t FatEntrySize = 0x10;
constexpr std::size_t FntHeaderSize = 0x8;
constexpr u16 KnownVersion = 0x0100;
// Loaders keep the file count in a 14-bit range.
constexpr u32 MaxFileCount = 0x4000;
constexpr u32 NameOffsetMask = 0x00FFFFFF;

struct Entry {
  u32 hash;
  u32 name_field;
  u32 begin;
  u32 end;
  std::string name;
};

void require_size(Analysis& out, std::string_view what, u32 actual, std::size_t expected) {
  if (actual != expected)
    out.errors.push_back(std::string(what) + " size " + fmt_hex(actual) + " is not " + fmt_hex(expected) +
                         ", which loaders reject");
}

u32 hash_name(u32 multiplier, std::string_view name) {
  u32 hash = 0;
  for (const char c : name)
    hash = hash * multiplier + static_cast<u32>(static_cast<signed char>(c));
  return hash;
}

}  // namespace

bool probe(Bytes data) {
  return ByteReader(data, Endian::Big).magic_at(0, "SARC");
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(Name);
  ByteReader r(data, Endian::Big);
  r.require(0, HeaderSize, "SARC header");

  const u8 bom0 = r.u8_at(0x6), bom1 = r.u8_at(0x7);
  if (bom0 == 0xFE && bom1 == 0xFF)
    r.set_endian(Endian::Big);
  else if (bom0 == 0xFF && bom1 == 0xFE)
    r.set_endian(Endian::Little);
  else
    throw ParseError("invalid byte order mark " + fmt_hex_bytes(data.subspan(6, 2)));

  const u16 header_size = r.u16_at(0x4);
  const u32 file_size = r.u32_at(0x8);
  const u32 data_offset = r.u32_at(0xC);
  const u16 version = r.u16_at(0x10);

  Section& s = out.section(std::string(Name));
  s.add("Byte Order", std::string(endian_name(r.endian())));
  s.add("Header Size", fmt_hex(header_size));
  s.add("Version", fmt_hex(version));
  s.add("File Size", fmt_size(file_size));
  s.add("Data Offset", fmt_hex(data_offset));

  require_size(out, "SARC header", header_size, HeaderSize);
  if (version != KnownVersion)
    out.errors.push_back("SARC version " + fmt_hex(version) + " is not " + fmt_hex(KnownVersion) +
                         ", which loaders reject");
  if (file_size != data.size())
    out.warnings.push_back("SARC declares " + fmt_hex(file_size) + " bytes but holds " + fmt_hex(data.size()));
  if (data_offset > data.size())
    throw ParseError("data offset " + fmt_hex(data_offset) + " lies beyond the end of the archive");

  const std::size_t fat = header_size;
  if (!r.magic_at(fat, "SFAT"))
    throw ParseError("SFAT block missing at " + fmt_hex(fat));
  r.require(fat, FatHeaderSize, "SFAT header");
  const u16 fat_header_size = r.u16_at(fat + 0x4);
  const u16 file_count = r.u16_at(fat + 0x6);
  const u32 multiplier = r.u32_at(fat + 0x8);
  require_size(out, "SFAT header", fat_header_size, FatHeaderSize);
  if (file_count >= MaxFileCount)
    out.errors.push_back("SFAT holds " + std::to_string(file_count) + " files; loaders accept fewer than " +
                         std::to_string(MaxFileCount));

  const std::size_t entries_at = fat + fat_header_size;
  r.require(entries_at, std::size_t(file_count) * FatEntrySize, "SFAT entries");
  const std::size_t fnt = entries_at + std::size_t(file_count) * FatEntrySize;
  if (!r.magic_at(fnt, "SFNT"))
    throw ParseError("SFNT block missing at " + fmt_hex(fnt));
  r.require(fnt, FntHeaderSize, "SFNT header");
  const u16 fnt_header_size = r.u16_at(fnt + 0x4);
  require_size(out, "SFNT header", fnt_header_size, FntHeaderSize);
  const std::size_t names = fnt + fnt_header_size;
  // Member data must not overlap the archive's own headers; loaders reject this.
  if (data_offset < names)
    throw ParseError("data offset " + fmt_hex(data_offset) + " precedes the end of the SFNT header at " +
                     fmt_hex(names));

  std::vector<Entry> entries;
  entries.reserve(file_count);
  std::size_t named = 0, hash_mismatches = 0, out_of_order = 0, collisions = 0, bad_ordinals = 0;
  u64 payload_bytes = 0;
  u32 run_ordinal = 0;
  for (std::size_t i = 0; i < file_count; ++i) {
    const std::size_t e = entries_at + i * FatEntrySize;
    Entry entry{r.u32_at(e), r.u32_at(e + 0x4), r.u32_at(e + 0x8), r.u32_at(e + 0xC), {}};

    // Entries are sorted by hash so lookups can binary search; equal hashes
    // form a run, and each entry's name field records its 1-based place in it.
    if (i != 0 && entry.hash < entries.back().hash)
      ++out_of_order;
    run_ordinal = (i != 0 && entry.hash == entries.back().hash) ? run_ordinal + 1 : 1;
    if (run_ordinal > 1)
      ++collisions;

    if (entry.end < entry.begin || u64(data_offset) + entry.end > data.size())
      throw ParseError("SFAT entry " + std::to_string(i) + " spans " + fmt_hex(entry.begin) + "-" +
                       fmt_hex(entry.end) + ", outside the data region");
    payload_bytes += entry.end - entry.begin;

    if (entry.name_field != 0) {
      ++named;
      if ((entry.name_field >> 24) != run_ordinal)
        ++bad_ordinals;
      const std::size_t name_at = names + std::size_t(entry.name_field & NameOffsetMask) * 4;
      if (name_at >= data_offset)
        throw ParseError("SFAT entry " + std::to_string(i) + " name offset " + fmt_hex(name_at) +
                         " lies outside the name table");
      entry.name = r.cstring_at(name_at, data_offset - name_at);
      if (hash_name(multiplier, entry.name) != entry.hash)
        ++hash_mismatches;
    }
    entries.push_back(std::move(entry));
  }

  s.add("Files", std::to_string(file_count));
  s.add("Named Files", std::to_string(named));
  s.add("Hash Multiplier", fmt_hex(multiplier));
  s.add("Hash Collisions", std::to_string(collisions));
  s.add("File Data Total", fmt_size(payload_bytes));

  if (out_of_order != 0)
    out.errors.push_back("SFAT entries are not sorted by name hash (" + std::to_string(out_of_order) +
                         " out of order), so lookups by name fail");
  if (hash_mismatches != 0)
    out.errors.push_back(std::to_string(hash_mismatches) + " SARC file names do not match their SFAT hash");
  if (bad_ordinals != 0)
    out.errors.push_back(std::to_string(bad_ordinals) +
                         " SFAT name fields carry the wrong collision index for their hash");

  if (entries.empty() || !ctx.list_members)
    return;
  Section& files = out.section(std::string(Name) + " Files");
  files.add("", member_header());
  for (const Entry& e : entries) {
    const std::size_t at = std::size_t(data_offset) + e.begin;
    const Bytes file = data.subspan(at, e.end - e.begin);
    const std::string name = e.name_field != 0 ? e.name : "(unnamed, hash " + fmt_hex(e.hash, 8) + ")";
    files.add("", member_row(at, file.size(), name, member_format(file, ctx)));
  }
}

}  // namespace nnfck::sarc
