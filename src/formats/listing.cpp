#include "formats/listing.hpp"

namespace nnfck::listing {

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

void add_groups(Analysis& out, std::string_view title, const std::vector<Group>& groups) {
  bool any = false;
  for (const Group& g : groups)
    any = any || !g.rows.empty() || !g.summary.empty();
  if (!any)
    return;
  Section& s = out.section(std::string(title));
  for (const Group& g : groups) {
    if (g.rows.empty() && g.summary.empty())
      continue;
    s.add(g.label, g.summary.empty() ? std::to_string(g.rows.size()) : g.summary);
    for (const Row& row : g.rows)
      s.add("", row.text, 1 + row.depth);
  }
}

}  // namespace nnfck::listing
