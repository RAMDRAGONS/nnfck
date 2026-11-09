#include "formats/fres/common.hpp"

namespace nnfck::fres {

namespace {

constexpr u32 AnimCurveBaked = 1u << 0;
constexpr u32 AnimPlayPolicyLoop = 1u << 2;

}  // namespace

void add_groups(Analysis& out, const std::vector<Group>& groups) {
  bool any = false;
  for (const Group& g : groups)
    any = any || !g.entries.empty();
  if (!any)
    return;
  Section& s = out.section(Title + " Contents");
  for (const Group& g : groups) {
    if (g.entries.empty())
      continue;
    s.add(g.label, std::to_string(g.entries.size()));
    for (const ListEntry& e : g.entries) {
      std::string row = e.data != 0 ? fmt_hex(e.data, 8) + "  " + e.name : e.name;
      if (!e.detail.empty())
        row += ": " + e.detail;
      s.add("", std::move(row), 1);
    }
  }
}

void add_external_files(Analysis& out, const std::vector<ListEntry>& names,
                        const std::vector<std::pair<u64, u64>>& spans, Bytes data, const Context& ctx) {
  if (spans.empty())
    return;
  Section& s = out.section(Title + " External Files");
  s.add("", member_header());
  for (std::size_t i = 0; i < spans.size(); ++i) {
    const auto [at, size] = spans[i];
    const std::string name = i < names.size() ? names[i].name : "(unnamed)";
    if (at > data.size() || size > data.size() - at) {
      out.errors.push_back("BFRES external file " + std::to_string(i) + " spans " + fmt_hex(at) + "+" +
                           fmt_hex(size) + ", outside the file");
      continue;
    }
    const Bytes file = data.subspan(static_cast<std::size_t>(at), static_cast<std::size_t>(size));
    s.add("", member_row(at, size, name, member_format(file, ctx)));
  }
}

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

std::size_t max_dict_entries(const ByteReader& r, std::size_t dict) {
  return dict < r.size() ? (r.size() - dict) / DictNodeSize : 0;
}

void add_anim_flags(std::vector<std::string>& parts, u32 flag) {
  if (flag & AnimPlayPolicyLoop)
    parts.push_back("looping");
  if (flag & AnimCurveBaked)
    parts.push_back("baked");
}

std::string frames(s32 n) {
  return count_of(static_cast<u64>(n < 0 ? 0 : n), "frame");
}

void expect_count(Analysis& out, const std::string& owner, std::string_view what, u32 declared, std::size_t listed,
                  std::string_view source) {
  if (declared != listed)
    out.warnings.push_back("BFRES " + owner + " counts " + std::to_string(declared) + " " + std::string(what) +
                           " but its " + std::string(source) + " lists " + std::to_string(listed));
}

u64 cafe_offset(const ByteReader& r, std::size_t field) {
  const s32 v = r.s32_at(field);
  return v == 0 ? 0 : static_cast<u64>(static_cast<std::int64_t>(field) + v);
}

std::string cafe_string(const ByteReader& r, std::size_t field) {
  const u64 at = cafe_offset(r, field);
  return at != 0 ? r.cstring_at(static_cast<std::size_t>(at), MaxNameLength) : std::string();
}

std::size_t cafe_dict_count(const ByteReader& r, std::size_t field) {
  const u64 dict = cafe_offset(r, field);
  return dict == 0 ? 0 : static_cast<std::size_t>(r.s32_at(static_cast<std::size_t>(dict) + 4));
}

std::string nx_string(const ByteReader& r, u64 at) {
  if (at == 0)
    return {};
  const u16 length = r.u16_at(static_cast<std::size_t>(at));
  r.require(static_cast<std::size_t>(at) + 2, length, "BFRES string");
  const Bytes s = r.data().subspan(static_cast<std::size_t>(at) + 2, length);
  return std::string(s.begin(), s.end());
}

std::size_t nx_dict_count(const ByteReader& r, std::size_t field) {
  const u64 dict = r.u64_at(field);
  return dict == 0 ? 0 : static_cast<std::size_t>(r.s32_at(static_cast<std::size_t>(dict) + 4));
}

}  // namespace nnfck::fres
