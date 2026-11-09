#include "formats/fres/intermediate.hpp"

#include <algorithm>

#include "core/xml.hpp"
#include "formats/listing.hpp"

namespace nnfck::fres::intermediate {

namespace {

using listing::Group;
using listing::Row;

constexpr std::string_view Root = "nw4f_3dif";
constexpr std::string_view StreamTableMagic = "g3dstrma";
constexpr std::string_view StreamMagic = "stream  ";
constexpr std::size_t StreamTableHeaderSize = 0xC;
constexpr std::size_t StreamEntrySize = 8;
constexpr std::size_t StreamHeaderSize = 0x18;
constexpr std::size_t MaxInfoRows = 48;

struct Resource {
  std::string_view element;
  std::string_view name;
};

constexpr Resource Resources[] = {
    {"model", "Cafe Model"},
    {"material", "Cafe Material"},
    {"texture", "Cafe Texture"},
    {"skeletal_anim", "Cafe Skeletal Animation"},
    {"shader_param_anim", "Cafe Shader Parameter Animation"},
    {"tex_pattern_anim", "Cafe Texture Pattern Animation"},
    {"bone_visibility_anim", "Cafe Bone Visibility Animation"},
    {"mat_visibility_anim", "Cafe Material Visibility Animation"},
    {"shape_anim", "Cafe Shape Animation"},
    {"scene_anim", "Cafe Scene Animation"},
    {"material_anim", "Cafe Material Animation"},
    {"shader_config", "Cafe Shader Config"},
    {"shader_definition", "Cafe Shader Definition"},
    {"shader_variation", "Cafe Shader Variation"},
    {"shader_graph", "Cafe Shader Graph"},
    {"transform_tree", "Cafe Transform Tree"},
    {"transform_tree_anim", "Cafe Transform Tree Animation"},
    {"transform_tree_visibility_anim", "Cafe Transform Tree Visibility Animation"},
};

constexpr std::string_view StreamTypes[] = {"float", "int", "byte", "string", "wstring"};

// Schema names are snake_case with abbreviated words.
std::string title(std::string_view key) {
  static constexpr std::pair<std::string_view, std::string_view> Words[] = {
      {"anim", "Animation"}, {"mat", "Material"}, {"vis", "Visibility"}, {"tex", "Texture"},
      {"src", "Source"},     {"dcc", "DCC"},      {"fps", "FPS"},        {"mip", "Mip"},
  };
  std::string out;
  std::size_t at = 0;
  while (at <= key.size()) {
    const std::size_t end = std::min(key.find('_', at), key.size());
    std::string_view word = key.substr(at, end - at);
    if (!word.empty()) {
      std::string w(word);
      for (const auto& [from, to] : Words)
        if (word == from)
          w = std::string(to);
      if (w[0] >= 'a' && w[0] <= 'z')
        w[0] = static_cast<char>(w[0] - 'a' + 'A');
      out += (out.empty() ? "" : " ") + w;
    }
    at = end + 1;
  }
  return out;
}

std::string plural(std::string word) {
  if (word.ends_with("Vertex") || word.ends_with("Index"))
    return word.substr(0, word.size() - 2) + "ices";
  if (word.ends_with("Data") || word.ends_with("s"))
    return word;
  return word + "s";
}

// The attribute that names an array item: its name, the name of what it
// targets, or a path.
std::string item_label(const xml::Element& item) {
  if (const std::string* n = item.attribute("name"))
    return *n;
  for (const auto& [k, v] : item.attributes)
    if (k.ends_with("_name") && k != "parent_name" && k != "base_name")
      return v;
  if (const std::string* p = item.attribute("path"))
    return *p;
  if (const std::string* p = item.attribute("process"))
    return *p;
  return {};
}

void add_arrays(const xml::Element& e, std::vector<Group>& groups, const Context& ctx, int depth) {
  for (const xml::Element& c : e.children) {
    const std::string_view tag = c.local_name();
    if (tag.ends_with("_array")) {
      Group g(plural(title(tag.substr(0, tag.size() - 6))));
      for (const xml::Element& item : c.children) {
        ctx.spend(8, "intermediate arrays");
        std::string label = item_label(item);
        if (tag == "stream_array") {
          label = item.attribute_or("type", "?") + ", " + item.attribute_or("count", "?") + " values";
        }
        if (!label.empty())
          g.rows.push_back({std::move(label)});
      }
      if (g.rows.size() != c.children.size())
        g.summary = std::to_string(c.children.size());
      if (!c.children.empty())
        groups.push_back(std::move(g));
    } else if (depth == 0 && (tag == "skeleton" || tag == "texture_info" || tag == "model_info")) {
      add_arrays(c, groups, ctx, depth + 1);
    }
  }
}

// Binary intermediates end the XML with a NUL, pad, and append a table of
// little-endian streams that stands in for the XML stream_array.
void read_streams(Bytes data, std::size_t xml_end, Section& s, std::vector<Group>& groups, Analysis& out,
                  const Context& ctx) {
  std::size_t at = xml_end;
  while (at < data.size() && data[at] == 0)
    ++at;
  ByteReader r(data, Endian::Little);
  if (at == data.size() || !r.has(at, StreamTableHeaderSize) || !r.magic_at(at, StreamTableMagic)) {
    out.errors.push_back("Binary intermediate has no stream table after its XML");
    return;
  }
  const u32 count = r.u32_at(at + 8);
  s.add("Stream Table", fmt_hex(at));
  s.add("Streams", std::to_string(count));
  if (u64{count} * StreamEntrySize > data.size() - at - StreamTableHeaderSize) {
    out.errors.push_back("Binary intermediate lists " + std::to_string(count) + " streams past the end of the file");
    return;
  }
  Group g("Streams");
  for (u32 i = 0; i < count; ++i) {
    ctx.spend(StreamEntrySize, "streams");
    const std::size_t e = at + StreamTableHeaderSize + std::size_t{i} * StreamEntrySize;
    const u64 offset = u64{at} + r.u32_at(e);
    const u32 size = r.u32_at(e + 4);
    if (offset > data.size() || size > data.size() - offset || size < StreamHeaderSize ||
        !r.magic_at(static_cast<std::size_t>(offset), StreamMagic)) {
      out.errors.push_back("Binary intermediate stream " + std::to_string(i) + " at " + fmt_hex(offset) +
                           " is not a stream inside the file");
      continue;
    }
    const std::size_t o = static_cast<std::size_t>(offset);
    const u32 type = r.u32_at(o + 8);
    const u32 values = r.u32_at(o + 0xC);
    const u32 bytes = r.u32_at(o + 0x14);
    if (bytes > size - StreamHeaderSize)
      out.errors.push_back("Binary intermediate stream " + std::to_string(i) + " holds " + fmt_hex(bytes) +
                           " bytes in a " + fmt_hex(size) + "-byte entry");
    const std::string type_name =
        type < std::size(StreamTypes) ? std::string(StreamTypes[type]) : "type " + std::to_string(type);
    g.rows.push_back({type_name + ", " + std::to_string(values) + " values, " + fmt_hex(bytes) + " bytes"});
  }
  groups.push_back(std::move(g));
}

}  // namespace

bool probe(Bytes data) {
  return xml::root_name(data) == Root;
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  const std::size_t xml_end = static_cast<std::size_t>(std::find(data.begin(), data.end(), u8{0}) - data.begin());
  const bool binary = xml_end < data.size();
  ctx.spend(xml_end, "XML");
  const xml::Element root = xml::parse(data.first(xml_end));

  const xml::Element* body = nullptr;
  for (const xml::Element& c : root.children)
    if (c.local_name() != "file_info")
      body = &c;
  if (body == nullptr)
    throw ParseError("intermediate file holds no resource");

  std::string name = "Cafe " + title(body->local_name());
  for (const Resource& r : Resources)
    if (r.element == body->local_name())
      name = std::string(r.name);
  if (binary)
    name += " Binary";
  out.format = name;

  Section& s = out.section(name);
  s.add("Version", root.attribute_or("version", "?"));
  if (const xml::Element* info = root.child("file_info")) {
    for (const std::string_view stage : {"create", "modify"}) {
      const xml::Element* e = info->child(stage);
      if (e == nullptr)
        continue;
      const std::string label = stage == "create" ? "Created" : "Modified";
      const std::string tool = e->attribute_or("tool_name");
      const std::string version = e->attribute_or("tool_version");
      if (!tool.empty())
        s.add(label + " With", version.empty() ? tool : tool + " " + version);
      if (const std::string date = e->attribute_or("date_time"); !date.empty())
        s.add(label, date);
    }
  }

  const std::string info_tag = std::string(body->local_name()) + "_info";
  if (const xml::Element* info = body->child(info_tag)) {
    std::size_t rows = 0;
    for (const auto& [k, v] : info->attributes) {
      if (v.empty())
        continue;
      if (++rows > MaxInfoRows)
        break;
      s.add(title(k), v);
    }
  }

  std::vector<Group> groups;
  add_arrays(*body, groups, ctx, 0);
  if (binary)
    read_streams(data, xml_end, s, groups, out, ctx);

  listing::add_groups(out, name + " Contents", groups);
}

}  // namespace nnfck::fres::intermediate
