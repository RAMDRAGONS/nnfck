#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/report.hpp"

// Contents sections: labelled groups, each a count followed by one row per
// entry.
namespace nnfck::listing {

struct Row {
  std::string text;
  // Nesting below the group's own rows, for trees such as the pane hierarchy.
  int depth = 0;
};

struct Group {
  explicit Group(std::string l) : label(std::move(l)) {}

  std::string label;
  std::vector<Row> rows;
  // Replaces the row count where rows are not all entries of the same kind.
  std::string summary;
};

// "1 frame", "2 frames"; `plural` overrides the trailing s.
std::string count_of(u64 n, std::string_view singular, std::string_view plural = {});
std::string join(const std::vector<std::string>& parts);

// Prints each group that has rows or a summary.
void add_groups(Analysis& out, std::string_view title, const std::vector<Group>& groups);

}  // namespace nnfck::listing
