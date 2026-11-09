#include <algorithm>
#include <map>

#include "formats/layout/common.hpp"

// nn::ui2d and nw::lyt layout resources: a chain of blocks holding the
// texture, font and material lists, a pane tree bracketed by pas1 and pae1,
// and a group tree bracketed by grs1 and gre1.
namespace nnfck::flyt {

namespace {

using namespace layout;

constexpr std::size_t LayoutBlockSize = 0x1C;
constexpr std::size_t ListHeaderSize = 0xC;
constexpr std::size_t PaneNameAt = 0xC;
constexpr std::size_t PaneSize = 0x54;
constexpr std::size_t PartsHeaderSize = 0x60;
constexpr std::size_t PartsPropertySize = 0x28;
constexpr std::size_t ControlHeaderSize = 0x1C;
constexpr std::size_t LegacyControlHeaderSize = 0x10;
constexpr u32 ControlUserNameVersion = 0x03000000;
constexpr std::size_t GroupHeaderSize = 0x2C;
constexpr std::size_t LegacyGroupHeaderSize = 0x24;
constexpr std::size_t LegacyGroupNameLength = 24;
constexpr std::size_t GroupNameLength = 33;
// Deep enough for any authored tree; keeps crafted nesting from producing
// unbounded indentation.
constexpr int MaxTreeDepth = 32;

struct PaneKind {
  std::string_view kind;
  // The kind attribute of the XML source's pane element.
  std::string_view text;
  std::string_view name;
};

constexpr PaneKind PaneKinds[] = {
    {"pan1", "Null", "Null"},           {"pic1", "Picture", "Picture"}, {"txt1", "TextBox", "Text Box"},
    {"wnd1", "Window", "Window"},       {"bnd1", "Bounding", "Bounding"}, {"prt1", "Parts", "Parts"},
    {"cpt1", "Capture", "Capture"},     {"ali1", "Alignment", "Alignment"}, {"scr1", "Scissor", "Scissor"},
};

const PaneKind* text_pane_kind(std::string_view kind) {
  for (const PaneKind& k : PaneKinds)
    if (k.text == kind)
      return &k;
  return nullptr;
}

const PaneKind* pane_kind(std::string_view kind) {
  for (const PaneKind& k : PaneKinds)
    if (k.kind == kind)
      return &k;
  return nullptr;
}

// Blocks this parser recognizes without reading their contents.
constexpr std::string_view OtherKinds[] = {"usd1", "spi1", "ctl1", "stm1", "vgl1"};

std::string origin_name(u8 origin) {
  switch (origin) {
  case 0:
    return "Top left";
  case 1:
    return "Center";
  default:
    return "Unknown (" + std::to_string(origin) + ")";
  }
}

std::string text_origin_name(std::string_view origin) {
  if (origin == "Classic")
    return "Top left";
  if (origin == "Normal")
    return "Center";
  return std::string(origin);
}

std::string text_size(const xml::Element* e) {
  return e == nullptr ? std::string() : e->attribute_or("x", "?") + " x " + e->attribute_or("y", "?");
}

struct TextPane {
  std::string kind;
  std::string parts;
};

// Materials sit inside each pane's kind-specific settings at varying depths.
void collect_materials(const xml::Element& e, Group& materials, int depth) {
  if (depth > MaxTreeDepth)
    return;
  for (const xml::Element& c : e.children) {
    if (c.local_name() == "material" && c.attribute("name"))
      materials.rows.push_back({*c.attribute("name")});
    else
      collect_materials(c, materials, depth + 1);
  }
}

void add_pane_tree(const xml::Element& tree, const std::map<std::string, TextPane>& panes, Group& out, int depth,
                   const Context& ctx) {
  for (const xml::Element* t : tree.children_named("paneTree")) {
    ctx.spend(PaneSize, "panes");
    const std::string name = t->attribute_or("name");
    std::string row = name;
    if (const auto it = panes.find(name); it != panes.end()) {
      row += ": " + it->second.kind;
      if (!it->second.parts.empty())
        row += " (" + it->second.parts + ")";
    } else if (depth == 0) {
      // The editor creates the root itself and never lists it with the panes.
      row += ": Null";
    }
    out.rows.push_back({std::move(row), std::min(depth, MaxTreeDepth)});
    if (depth < MaxTreeDepth)
      add_pane_tree(*t, panes, out, depth + 1, ctx);
  }
}

void add_group_tree(const xml::Element& set, Group& out, int depth) {
  for (const xml::Element* g : set.children_named("group")) {
    out.rows.push_back({g->attribute_or("name") + ": " + count_of(g->children_named("paneRef").size(), "pane"),
                        std::min(depth, MaxTreeDepth)});
    if (depth < MaxTreeDepth)
      add_group_tree(*g, out, depth + 1);
  }
}

std::string size_string(const ByteReader& r, std::size_t at) {
  return fmt_float(r.f32_at(at)) + " x " + fmt_float(r.f32_at(at + 4));
}

// Group blocks widened their name from 24 bytes to 33 plus padding; the block
// size, which covers the 24-byte pane names that follow, tells which.
std::size_t group_header_size(const Block& b, u16 legacy_count, u16 count) {
  if (b.size == GroupHeaderSize + std::size_t{count} * ResourceNameLength)
    return GroupHeaderSize;
  if (b.size == LegacyGroupHeaderSize + std::size_t{legacy_count} * ResourceNameLength)
    return LegacyGroupHeaderSize;
  return 0;
}

}  // namespace

bool probe(Bytes data) {
  return layout::probe(data, "FLYT");
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  ByteReader r(data, Endian::Big);
  Section& s = out.section(std::string(Name));
  const FileHeader h = read_header(r, Name, out, s, data.size());
  const std::vector<Block> blocks = read_blocks(r, h, "Layout", out, ctx);

  Group textures{"Textures"};
  Group fonts{"Fonts"};
  Group materials{"Materials"};
  Group panes{"Panes"};
  Group groups{"Groups"};
  Group parts_layouts{"Parts Layouts"};
  std::map<std::string_view, u32> pane_counts;
  std::vector<std::string> parts_seen;
  u32 user_data_blocks = 0;
  int pane_depth = 0;
  int group_depth = 0;
  bool have_layout = false;

  for (const Block& b : blocks) {
    const std::size_t end = b.offset + b.size;
    if (b.kind == "lyt1") {
      r.require(b.offset, LayoutBlockSize, "layout block");
      have_layout = true;
      if (b.size > LayoutBlockSize)
        s.add("Name", r.cstring_at(b.offset + LayoutBlockSize, std::min(MaxNameLength, b.size - LayoutBlockSize)));
      s.add("Origin", origin_name(r.u8_at(b.offset + 8)));
      s.add("Layout Size", size_string(r, b.offset + 0xC));
      s.add("Parts Size", size_string(r, b.offset + 0x14));
    } else if (b.kind == "cnt1") {
      // Version 2 controls have no user name, so their counts sit right
      // after the pane name offset.
      const bool legacy = h.version < ControlUserNameVersion;
      const std::size_t header = legacy ? LegacyControlHeaderSize : ControlHeaderSize;
      const std::size_t counts = legacy ? 0xC : 0x10;
      r.require(b.offset, header, "control block");
      const std::string control = r.cstring_at(b.offset + header, std::min(MaxNameLength, b.size - header));
      s.add("Control", control);
      if (!legacy) {
        const u32 user_at = r.u32_at(b.offset + 8);
        if (user_at >= header && user_at < b.size) {
          const std::string user = r.cstring_at(b.offset + user_at, std::min(MaxNameLength, b.size - user_at));
          if (user != control)
            s.add("Control User Name", user);
        }
      }
      s.add("Control Panes", std::to_string(r.u16_at(b.offset + counts)));
      s.add("Control Animations", std::to_string(r.u16_at(b.offset + counts + 2)));
    } else if (b.kind == "txl1" || b.kind == "fnl1") {
      r.require(b.offset, ListHeaderSize, "list block");
      Group& g = b.kind == "txl1" ? textures : fonts;
      for (std::string& n :
           offset_names(r, b.offset + ListHeaderSize, r.u16_at(b.offset + 8), end, g.label, ctx))
        g.rows.push_back({std::move(n)});
    } else if (b.kind == "mat1") {
      r.require(b.offset, ListHeaderSize, "material list");
      const u16 count = r.u16_at(b.offset + 8);
      r.require(b.offset + ListHeaderSize, std::size_t{count} * 4, "material offsets");
      for (u16 i = 0; i < count; ++i) {
        const u32 at = r.u32_at(b.offset + ListHeaderSize + std::size_t{i} * 4);
        if (at < ListHeaderSize || at > b.size - std::min(b.size, MaterialNameLength)) {
          out.errors.push_back("Layout material " + std::to_string(i) + " at " + fmt_hex(at) +
                               " lies outside its list");
          continue;
        }
        ctx.spend(MaterialNameLength, "materials");
        materials.rows.push_back({fixed_name(r, b.offset + at, MaterialNameLength)});
      }
    } else if (b.kind == "pas1") {
      ++pane_depth;
    } else if (b.kind == "pae1") {
      if (pane_depth == 0)
        out.errors.push_back("Layout pane tree closes at " + fmt_hex(b.offset) + " without being opened");
      else
        --pane_depth;
    } else if (b.kind == "grs1") {
      ++group_depth;
    } else if (b.kind == "gre1") {
      if (group_depth == 0)
        out.errors.push_back("Layout group tree closes at " + fmt_hex(b.offset) + " without being opened");
      else
        --group_depth;
    } else if (const PaneKind* k = pane_kind(b.kind)) {
      r.require(b.offset, PaneSize, "pane");
      ctx.spend(PaneSize, "panes");
      ++pane_counts[k->name];
      std::string row = fixed_name(r, b.offset + PaneNameAt, ResourceNameLength) + ": " + std::string(k->name);
      if (b.kind == "prt1") {
        r.require(b.offset, PartsHeaderSize, "parts pane");
        const u64 name_at = PartsHeaderSize + u64{r.u32_at(b.offset + PaneSize)} * PartsPropertySize;
        if (name_at >= b.size) {
          out.errors.push_back("Layout parts pane at " + fmt_hex(b.offset) + " has properties past its block");
        } else {
          std::string layout = r.cstring_at(b.offset + name_at, std::min<u64>(MaxNameLength, b.size - name_at));
          row += " (" + layout + ")";
          if (std::find(parts_seen.begin(), parts_seen.end(), layout) == parts_seen.end()) {
            parts_seen.push_back(layout);
            parts_layouts.rows.push_back({std::move(layout)});
          }
        }
      }
      panes.rows.push_back({std::move(row), std::clamp(pane_depth, 0, MaxTreeDepth)});
    } else if (b.kind == "grp1") {
      r.require(b.offset, LegacyGroupHeaderSize, "group");
      const u16 legacy_count = r.u16_at(b.offset + 8 + LegacyGroupNameLength);
      const u16 count = b.size >= GroupHeaderSize ? r.u16_at(b.offset + 8 + GroupNameLength + 1) : 0;
      const std::size_t header = group_header_size(b, legacy_count, count);
      if (header == 0) {
        out.errors.push_back("Layout group at " + fmt_hex(b.offset) + " has size " + fmt_hex(b.size) +
                             ", which fits neither group layout");
        continue;
      }
      const bool legacy = header == LegacyGroupHeaderSize;
      const std::string name =
          fixed_name(r, b.offset + 8, legacy ? LegacyGroupNameLength : GroupNameLength);
      ctx.spend(b.size, "groups");
      groups.rows.push_back({name + ": " + count_of(legacy ? legacy_count : count, "pane"),
                             std::clamp(group_depth, 0, MaxTreeDepth)});
    } else if (b.kind == "usd1") {
      ++user_data_blocks;
    } else if (std::find(std::begin(OtherKinds), std::end(OtherKinds), b.kind) == std::end(OtherKinds)) {
      out.warnings.push_back("Layout block " + fmt_quoted(r.data().subspan(b.offset, 4)) + " at " +
                             fmt_hex(b.offset) + " is not recognized");
    }
  }

  if (!have_layout)
    out.errors.push_back("Layout has no lyt1 block");
  if (pane_depth != 0)
    out.errors.push_back("Layout pane tree is not closed");
  if (group_depth != 0)
    out.errors.push_back("Layout group tree is not closed");

  s.add("Textures", std::to_string(textures.rows.size()));
  s.add("Fonts", std::to_string(fonts.rows.size()));
  s.add("Materials", std::to_string(materials.rows.size()));
  std::vector<std::string> kinds;
  for (const PaneKind& k : PaneKinds)
    if (const auto it = pane_counts.find(k.name); it != pane_counts.end())
      kinds.push_back(std::to_string(it->second) + " " + std::string(k.name));
  s.add("Panes", std::to_string(panes.rows.size()) + (kinds.empty() ? "" : " (" + join(kinds) + ")"));
  s.add("Groups", std::to_string(groups.rows.size()));
  if (user_data_blocks != 0)
    s.add("User Data Lists", std::to_string(user_data_blocks));

  add_groups(out, std::string(Name) + " Contents", {textures, fonts, materials, parts_layouts, panes, groups});
}

bool probe_text(Bytes data) {
  return text_body_kind(data) == "lyt";
}

void analyze_text(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(TextName);
  const xml::Element root = parse_text(data, ctx);
  const xml::Element* body = text_body(root);
  const xml::Element* lyt = body ? body->child("lyt") : nullptr;
  if (lyt == nullptr)
    throw ParseError("no lyt element in the layout body");

  Section& s = out.section(std::string(TextName));
  add_text_header(root, s);
  if (const xml::Element* screen = lyt->child("screenSetting")) {
    if (const std::string* origin = screen->attribute("origin"))
      s.add("Origin", text_origin_name(*origin));
    if (const xml::Element* size = screen->child("layoutSize"))
      s.add("Layout Size", text_size(size));
  }
  if (const xml::Element* parts = lyt->child("partsSize"))
    s.add("Parts Size", text_size(parts));
  for (const xml::Element* control : lyt->children_named("control")) {
    const std::string name = control->attribute_or("name");
    const std::string user = control->attribute_or("userName");
    s.add("Control", user.empty() || user == name ? name : name + " (" + user + ")");
  }

  Group textures("Textures");
  Group fonts("Fonts");
  Group materials("Materials");
  Group parts_layouts("Parts Layouts");
  Group panes("Panes");
  Group groups("Groups");
  for (const xml::Element* t : lyt->children_named("textureFile")) {
    std::string row = t->attribute_or("imagePath");
    if (const std::string* format = t->attribute("format"))
      row += " (" + *format + ")";
    textures.rows.push_back({std::move(row)});
  }
  for (const xml::Element* f : lyt->children_named("fontFile"))
    fonts.rows.push_back({f->attribute_or("path")});

  std::map<std::string, TextPane> pane_info;
  std::map<std::string_view, u32> pane_counts;
  if (const xml::Element* set = lyt->child("paneSet")) {
    for (const xml::Element* p : set->children_named("pane")) {
      ctx.spend(PaneSize, "panes");
      const std::string kind = p->attribute_or("kind");
      const PaneKind* k = text_pane_kind(kind);
      TextPane info{k ? std::string(k->name) : kind, {}};
      ++pane_counts[k ? k->name : std::string_view("Other")];
      if (const xml::Element* parts = p->child("parts")) {
        info.parts = parts->attribute_or("path");
        if (!info.parts.empty() &&
            std::none_of(parts_layouts.rows.begin(), parts_layouts.rows.end(),
                         [&](const Row& r) { return r.text == info.parts; }))
          parts_layouts.rows.push_back({info.parts});
      }
      collect_materials(*p, materials, 0);
      pane_info.emplace(p->attribute_or("name"), std::move(info));
    }
  }
  if (const xml::Element* hierarchy = lyt->child("paneHierarchy"))
    add_pane_tree(*hierarchy, pane_info, panes, 0, ctx);
  if (const xml::Element* set = lyt->child("groupSet"))
    add_group_tree(*set, groups, 0);

  s.add("Textures", std::to_string(textures.rows.size()));
  s.add("Fonts", std::to_string(fonts.rows.size()));
  s.add("Materials", std::to_string(materials.rows.size()));
  std::vector<std::string> kinds;
  for (const PaneKind& k : PaneKinds)
    if (const auto it = pane_counts.find(k.name); it != pane_counts.end())
      kinds.push_back(std::to_string(it->second) + " " + std::string(k.name));
  if (const auto it = pane_counts.find("Other"); it != pane_counts.end())
    kinds.push_back(std::to_string(it->second) + " other");
  s.add("Panes", std::to_string(pane_info.size()) + (kinds.empty() ? "" : " (" + join(kinds) + ")"));
  s.add("Groups", std::to_string(groups.rows.size()));
  if (panes.rows.size() < pane_info.size())
    out.warnings.push_back("Layout hierarchy places " + std::to_string(panes.rows.size()) + " panes but " +
                           std::to_string(pane_info.size()) + " are defined");

  add_groups(out, std::string(TextName) + " Contents", {textures, fonts, materials, parts_layouts, panes, groups});
}

}  // namespace nnfck::flyt
