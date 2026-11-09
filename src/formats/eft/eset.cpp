#include "formats/eft/eset.hpp"

#include <algorithm>

#include "core/xml.hpp"
#include "formats/listing.hpp"

namespace nnfck::eset {

namespace {

using listing::Group;

constexpr std::string_view CafeRoot = "EmitterSet";
constexpr std::string_view NxRoot = "EmitterSetData";
constexpr int MaxTreeDepth = 16;

constexpr std::string_view TextureExtensions[] = {".ftxa", ".ftxb", ".tga", ".png", ".dds", ".tif", ".tiff"};
constexpr std::string_view PrimitiveExtensions[] = {".fmda", ".fmdb"};

bool ends_with_any(std::string_view text, std::span<const std::string_view> extensions) {
  std::string lower(text);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  });
  return std::any_of(extensions.begin(), extensions.end(), [&](std::string_view e) { return lower.ends_with(e); });
}

std::string child_text(const xml::Element& e, std::string_view name) {
  const xml::Element* c = e.child(name);
  return c ? c->text : std::string();
}

void add_unique(Group& g, std::string text) {
  if (std::none_of(g.rows.begin(), g.rows.end(), [&](const listing::Row& r) { return r.text == text; }))
    g.rows.push_back({std::move(text)});
}

// File references sit at different depths in the two schemas, so every
// element's text is checked.
void collect_files(const xml::Element& e, Group& textures, Group& primitives, int depth) {
  if (depth > 64)
    return;
  if (e.children.empty() && !e.text.empty()) {
    if (ends_with_any(e.text, TextureExtensions))
      add_unique(textures, e.text);
    else if (ends_with_any(e.text, PrimitiveExtensions))
      add_unique(primitives, e.text);
  }
  for (const xml::Element& c : e.children)
    collect_files(c, textures, primitives, depth + 1);
}

// Switch emitters nest their children in an EmitterList of their own.
void add_emitters(const xml::Element& list, bool nx, Group& emitters, int depth, const Context& ctx) {
  if (depth > MaxTreeDepth)
    return;
  for (const xml::Element& e : list.children) {
    ctx.spend(8, "emitters");
    std::string row = child_text(e, nx ? "Name" : "name");
    if (!nx)
      if (const std::string type = child_text(e, "type"); !type.empty())
        row += ": " + type;
    if (nx && child_text(e, "EnableConvert") == "False")
      row += " (not converted)";
    emitters.rows.push_back({std::move(row), depth});
    if (const xml::Element* children = e.child("EmitterList"))
      add_emitters(*children, nx, emitters, depth + 1, ctx);
  }
}

}  // namespace

bool probe(Bytes data) {
  const std::string root = xml::root_name(data);
  return root == CafeRoot || root == NxRoot;
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(Name);
  ctx.spend(data.size(), "XML");
  const xml::Element root = xml::parse(data);
  const bool nx = root.local_name() == NxRoot;

  Section& s = out.section(std::string(Name));
  s.add("Schema", nx ? "Switch (EmitterSetData)" : "Wii U (EmitterSet)");
  s.add("Version", nx ? root.attribute_or("Version", "?") : child_text(root, "Version"));
  if (nx && child_text(root, "EnableConvert") == "False")
    s.add("Converted", "No");

  Group emitters("Emitters");
  Group textures("Textures");
  Group primitives("Primitives");
  if (const xml::Element* list = root.child("EmitterList"))
    add_emitters(*list, nx, emitters, 0, ctx);
  collect_files(root, textures, primitives, 0);
  s.add("Emitters", std::to_string(emitters.rows.size()));
  s.add("Textures", std::to_string(textures.rows.size()));
  s.add("Primitives", std::to_string(primitives.rows.size()));
  listing::add_groups(out, std::string(Name) + " Contents", {emitters, textures, primitives});
}

}  // namespace nnfck::eset
