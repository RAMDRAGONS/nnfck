#include "formats/pmaa/common.hpp"

#include "formats/pmaa/pmaa.hpp"

namespace nnfck::pmaa {

namespace {

using listing::count_of;

constexpr std::size_t MaxReportedProblems = 8;

struct Extension {
  std::string_view extension;
  std::string_view name;
};

// Named after the library or game namespace that reads each extension.
constexpr Extension Extensions[] = {
    {"aglenv", "Agl Environment"},
    {"aglenvset", "Agl Environment Set"},
    {"aglcube", "Agl Cube Map"},
    {"aglccr", "Agl Color Correction"},
    {"agldof", "Agl Depth of Field"},
    {"aglblm", "Agl Bloom"},
    {"aglatex", "Agl Auto Exposure"},
    {"aglfila", "Agl Anti-Aliasing Filter"},
    {"aglflr", "Agl Flare Filter"},
    {"aglglr", "Agl Glare Filter"},
    {"ksky", "Agl Sky"},
    {"aglmf", "Agl Multi Filter"},
    {"aglcam", "Agl Camera"},
    {"aglofx", "Agl Occluded Effect"},
    {"aglclwd", "Agl Cloud"},
    {"aglvolm", "Agl Volume Mask"},
    {"agldecd", "Agl Decal Drawer"},
    {"aglNmdw", "Agl Normal Drawer"},
    {"agllref", "Agl Local Reflection"},
    {"agllmap", "Agl Light Map"},
    {"pref", "Agl Planar Reflection"},
    {"aglsdw", "Agl Depth Shadow"},
    {"aglprojsdw", "Agl Projection Shadow"},
    {"aglshpp", "Agl Shadow Pre-Pass"},
    {"aglssao", "Agl SSAO"},
    // The extension of parameter IO classes that never set their own.
    {"xml", "XML"},
    {"genv", "Gsys Environment"},
    {"glght", "Gsys Light"},
    {"gsdw", "Gsys Shadow"},
    {"gsysdclao", "Gsys Decal Ambient Occlusion"},
    {"gmsconf", "Gsys Model Scene Config"},
    {"ptclconf", "Gsys Particle Config"},
    {"glpbm", "Gsys Light Probe"},
    {"glpbd", "Gsys Light Probe Volume"},
    {"gapkginfo", "Gsys Application Package Info"},
    {"gapkglist", "Gsys Application Package List"},
    {"lpdcam", "LunchPack Debug Camera"},
    {"sms", "Seal Shadow"},
    {"gmbppos", "Gambit Player Position"},
};

}  // namespace

bool is_string(u32 type) {
  return type == 7 || type == 8 || type == 15 || type == StringRefType;
}

bool is_buffer(u32 type) {
  return type == 13 || type == 14 || type == 18 || type == BinaryBufferType;
}

bool is_curve(u32 type) {
  return type >= Curve1Type && type < Curve1Type + 4;
}

std::size_t float_components(u32 type) {
  switch (type) {
  case 3: return 2;
  case 4: return 3;
  case 5:
  case 6:
  case 16: return 4;
  default: return 0;
  }
}

std::optional<std::size_t> string_capacity(u32 type) {
  switch (type) {
  case 7: return 31;
  case 8: return 63;
  case 15: return 255;
  default: return std::nullopt;
  }
}

std::optional<u32> param_type(std::string_view name) {
  for (u32 i = 0; i < ParamTypeCount; ++i)
    if (ParamTypes[i] == name)
      return i;
  return std::nullopt;
}

std::string extension_label(std::string_view extension, bool binary) {
  const std::string file = std::string(binary ? ".b" : ".") + std::string(extension);
  if (extension.empty())
    return "(none)";
  for (const Extension& e : Extensions)
    if (e.extension == extension)
      return (binary ? "Binary " : "") + std::string(e.name) + " (" + file + ")";
  return file;
}

void Contents::list(const std::string& name, int depth, u64 child_lists, u64 child_objects) {
  ++lists;
  if (depth == 1)
    m_lists.rows.push_back({name + ": " + count_of(child_lists, "list") + ", " + count_of(child_objects, "object")});
}

void Contents::object(const std::string& name, int depth, u64 child_params) {
  ++objects;
  if (depth == 1)
    m_objects.rows.push_back({name + ": " + count_of(child_params, "parameter")});
}

void Contents::param(u32 type) {
  ++params;
  if (type < ParamTypeCount)
    ++m_by_type[type];
}

void Contents::problem(std::string message) {
  if (m_problems++ < MaxReportedProblems)
    m_out.errors.push_back(std::move(message));
}

void Contents::finish(Section& s, const Context& ctx) {
  std::vector<std::string> parts;
  for (std::size_t i = 0; i < m_by_type.size(); ++i)
    if (m_by_type[i] != 0)
      parts.push_back(std::to_string(m_by_type[i]) + " " + std::string(ParamTypes[i]));
  if (!parts.empty())
    s.add("Parameter Types", listing::join(parts));
  if (m_problems > MaxReportedProblems)
    m_out.errors.push_back(std::to_string(m_problems - MaxReportedProblems) + " further parameter archive errors");
  if (ctx.list_members)
    listing::add_groups(m_out, std::string(Name) + " Contents", {m_lists, m_objects});
}

}  // namespace nnfck::pmaa
