#include "formats/bfres/common.hpp"

namespace nnfck::bfres {

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
    for (const ListEntry& e : g.entries)
      s.add("", e.detail.empty() ? e.name : e.name + ": " + e.detail, 1);
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

std::string count_of(u64 n, std::string_view singular, std::string_view plural) {
  std::string word(n == 1 ? singular : (plural.empty() ? std::string(singular) + "s" : std::string(plural)));
  return std::to_string(n) + " " + word;
}

std::string join(const std::vector<std::string>& parts) {
  std::string out;
  for (const std::string& p : parts) {
    if (!out.empty())
      out += ", ";
    out += p;
  }
  return out;
}

void add_anim_flags(std::vector<std::string>& parts, u32 flag) {
  if (flag & AnimPlayPolicyLoop)
    parts.push_back("looping");
  if (flag & AnimCurveBaked)
    parts.push_back("baked");
}

}  // namespace nnfck::bfres
