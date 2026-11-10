#include "formats/pmaa/names.hpp"

#include <unordered_map>

#include "core/crc32.hpp"
#include "core/data.hpp"
#include "core/file.hpp"
#include "core/json.hpp"
#include "core/report.hpp"

namespace nnfck::pmaa {

namespace {

// Repeated entries carry an index after a base name, directly or after an
// underscore; an empty base stands for entries named by index alone.
constexpr double MaxIndexLimit = 4096;

struct Table {
  std::unordered_map<u32, std::string> names;
  std::string problem;
};

std::vector<std::string> strings(const json::Value& doc, std::string_view key) {
  std::vector<std::string> out;
  const json::Value* v = doc.member(key);
  if (v == nullptr)
    return out;
  if (!v->is(json::Value::Kind::Array))
    throw ParseError("\"" + std::string(key) + "\" is not an array");
  for (const json::Value& e : v->items) {
    if (!e.is(json::Value::Kind::String))
      throw ParseError("\"" + std::string(key) + "\" holds a value that is not a string");
    out.push_back(e.string);
  }
  return out;
}

void load(Table& t, const json::Value& doc) {
  if (!doc.is(json::Value::Kind::Object))
    throw ParseError("the document is not an object");
  int max_index = 0;
  if (const json::Value* m = doc.member("max_index")) {
    const double n = m->number;
    if (!m->is(json::Value::Kind::Number) || !(n >= 0 && n <= MaxIndexLimit) ||
        n != static_cast<double>(static_cast<int>(n)))
      throw ParseError("\"max_index\" is not a whole number from 0 to " + std::to_string(int(MaxIndexLimit)));
    max_index = static_cast<int>(n);
  }
  const auto add = [&](std::string name) {
    const u32 hash = crc32(name);
    t.names.emplace(hash, std::move(name));
  };
  for (std::string& n : strings(doc, "names"))
    add(std::move(n));
  const std::vector<std::string> indexed = strings(doc, "indexed");
  const std::vector<std::string> underscored = strings(doc, "underscore_indexed");
  for (int i = 0; i < max_index; ++i) {
    const std::string index = std::to_string(i);
    for (const std::string& base : indexed)
      add(base + index);
    for (const std::string& base : underscored)
      add(base + "_" + index);
  }
}

const Table& table() {
  static const Table t = [] {
    Table out;
    const std::optional<std::filesystem::path> path = data::find(NamesFile);
    if (!path) {
      out.problem = std::string(NamesFile) + " was not found";
      return out;
    }
    try {
      const std::vector<u8> bytes = read_file(*path);
      load(out, json::parse(Bytes(bytes)));
    } catch (const std::exception& e) {
      out.names.clear();
      out.problem = path->string() + ": " + e.what();
    }
    return out;
  }();
  return t;
}

}  // namespace

const std::string* name_of(u32 hash) {
  const auto& names = table().names;
  const auto it = names.find(hash);
  return it == names.end() ? nullptr : &it->second;
}

std::string display_name(u32 hash) {
  if (const std::string* n = name_of(hash))
    return *n;
  return fmt_hex(hash, 8);
}

const std::string& names_problem() {
  return table().problem;
}

}  // namespace nnfck::pmaa
