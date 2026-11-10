#include <charconv>

#include "core/xml.hpp"
#include "formats/pmaa/common.hpp"
#include "formats/pmaa/pmaa.hpp"

// The XML form agl's parameter IO saves and its binary converter reads: a
// root element holding a header and a data element, then param_list,
// param_array and param elements for lists, objects and parameters, all
// named in plain text.
namespace nnfck::pmaa {

namespace {

constexpr std::string_view TextRoot = "root";
constexpr std::string_view ListTag = "param_list";
constexpr std::string_view ObjectTag = "param_array";
constexpr std::string_view ParamTag = "param";
constexpr std::string_view TimeStampPrefix = "time stamp : ";
constexpr std::size_t ElementCost = 16;

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::vector<std::string_view> tokens(std::string_view s) {
  std::vector<std::string_view> out;
  std::size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && is_space(s[i]))
      ++i;
    const std::size_t start = i;
    while (i < s.size() && !is_space(s[i]))
      ++i;
    if (i > start)
      out.push_back(s.substr(start, i - start));
  }
  return out;
}

template <typename T>
bool parses_as(std::string_view token) {
  T v{};
  const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), v);
  return ec == std::errc() && end == token.data() + token.size();
}

bool all_floats(const std::vector<std::string_view>& t) {
  for (const std::string_view s : t)
    if (!parses_as<float>(s))
      return false;
  return true;
}

class TextWalker {
public:
  TextWalker(Analysis& out, const Context& ctx) : contents(out), m_ctx(ctx) {}

  Contents contents;

  void list(const xml::Element& e, int depth) {
    if (depth > MaxDepth)
      throw ParseError("parameter lists nest deeper than " + std::to_string(MaxDepth) + " levels");
    m_ctx.spend(ElementCost, "parameter lists");
    const std::string name = e.attribute_or("name");
    contents.list(name, depth, e.children_named(ListTag).size(), e.children_named(ObjectTag).size());
    for (const xml::Element& c : e.children) {
      if (c.local_name() == ListTag)
        list(c, depth + 1);
      else if (c.local_name() == ObjectTag)
        object(c, depth + 1);
      else
        contents.problem("Parameter list " + name + " holds an unexpected <" + c.name + "> element");
    }
  }

private:
  const Context& m_ctx;

  void object(const xml::Element& e, int depth) {
    m_ctx.spend(ElementCost, "parameter objects");
    const std::string name = e.attribute_or("name");
    contents.object(name, depth, e.children_named(ParamTag).size());
    for (const xml::Element& c : e.children) {
      if (c.local_name() == ParamTag)
        param(c, name);
      else
        contents.problem("Parameter object " + name + " holds an unexpected <" + c.name + "> element");
    }
  }

  void param(const xml::Element& e, const std::string& object) {
    m_ctx.spend(ElementCost + e.text.size(), "parameters");
    const std::string where = "Parameter " + object + "/" + e.attribute_or("name");
    const std::string type_name = e.attribute_or("type");
    const std::optional<u32> type = param_type(type_name);
    if (!type) {
      contents.param(ParamTypeCount);
      contents.problem(where + " has unknown type \"" + type_name + "\"");
      return;
    }
    contents.param(*type);
    const std::string* value = e.attribute("value");
    if (is_curve(*type)) {
      check_curve(where, *type, e.text);
      return;
    }
    if (value == nullptr) {
      contents.problem(where + " has no value");
      return;
    }
    if (!value_valid(*type, *value))
      contents.problem(where + " holds \"" + *value + "\", which is not a valid " + type_name);
  }

  bool value_valid(u32 type, const std::string& value) {
    if (type == BoolType)
      return value == "true" || value == "false";
    if (type == F32Type)
      return parses_as<float>(value);
    if (type == IntType)
      return parses_as<s32>(value);
    // Buffers state their element count in the value and carry the elements
    // as content.
    if (type == U32Type || is_buffer(type))
      return parses_as<u32>(value);
    if (const std::size_t n = float_components(type)) {
      const std::vector<std::string_view> t = tokens(value);
      return t.size() == n && all_floats(t);
    }
    if (const std::optional<std::size_t> cap = string_capacity(type))
      return value.size() <= *cap;
    return true;
  }

  // Each curve is a line with its used point count and curve type, then its
  // used points as rows of three floats, the last row padded.
  void check_curve(const std::string& where, u32 type, const std::string& text) {
    const std::size_t curves = type - Curve1Type + 1;
    const std::vector<std::string_view> t = tokens(text);
    std::size_t at = 0;
    for (std::size_t c = 0; c < curves; ++c) {
      u32 used = 0;
      if (t.size() - at < 2 || !parses_as<u32>(t[at + 1]) ||
          std::from_chars(t[at].data(), t[at].data() + t[at].size(), used).ec != std::errc()) {
        contents.problem(where + " curve " + std::to_string(c) + " lacks its point count and curve type");
        return;
      }
      if (used > CurvePoints) {
        contents.problem(where + " curve " + std::to_string(c) + " uses " + std::to_string(used) + " of " +
                         std::to_string(CurvePoints) + " points");
        return;
      }
      at += 2;
      const std::size_t floats = (std::size_t{used} + 2) / 3 * 3;
      bool ok = t.size() - at >= floats;
      for (std::size_t i = 0; ok && i < floats; ++i)
        ok = parses_as<float>(t[at + i]);
      if (!ok) {
        contents.problem(where + " curve " + std::to_string(c) + " does not hold its " + std::to_string(floats) +
                         " floats");
        return;
      }
      at += floats;
    }
    if (at != t.size())
      contents.problem(where + " holds " + std::to_string(t.size() - at) + " numbers after its curves");
  }
};

bool contains(Bytes data, std::string_view s) {
  const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
  return text.find(s) != std::string_view::npos;
}

}  // namespace

bool probe_text(Bytes data) {
  return xml::root_name(data) == TextRoot && contains(data, "<header") && contains(data, "<param_list");
}

void analyze_text(Bytes data, Analysis& out, const Context& ctx) {
  ctx.spend(data.size(), "XML");
  const xml::Element root = xml::parse(data);
  const xml::Element* header = root.child("header");
  const xml::Element* body = root.child("data");
  if (header == nullptr || body == nullptr)
    throw ParseError("text parameter archive lacks its header or data element");

  out.format = std::string(Name);
  Section& s = out.section(std::string(Name));
  s.add("Extension", extension_label(header->attribute_or("type"), false));
  s.add("Data Version", header->attribute_or("version", "?"));
  if (std::string date = header->attribute_or("date"); !date.empty()) {
    if (date.starts_with(TimeStampPrefix))
      date.erase(0, TimeStampPrefix.size());
    s.add("Saved", date);
  }

  const std::vector<const xml::Element*> roots = body->children_named(ListTag);
  if (roots.size() != 1)
    throw ParseError("text parameter archive holds " + std::to_string(roots.size()) + " root lists instead of one");
  if (roots.front()->attribute_or("name") != RootName)
    out.warnings.push_back("Parameter archive root is " + roots.front()->attribute_or("name") + " rather than " +
                           std::string(RootName));

  TextWalker w(out, ctx);
  w.list(*roots.front(), 0);
  s.add("Lists", std::to_string(w.contents.lists));
  s.add("Objects", std::to_string(w.contents.objects));
  s.add("Parameters", std::to_string(w.contents.params));
  w.contents.finish(s, ctx);
}

}  // namespace nnfck::pmaa
