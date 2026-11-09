#include <algorithm>

#include "formats/layout/common.hpp"

// nn::ui2d and nw::lyt layout animations: a pat1 tag naming the segment and
// its groups, an optional pah1 share block, and a pai1 block with the
// animated panes and materials.
namespace nnfck::flan {

namespace {

using namespace layout;

constexpr std::size_t TagHeaderSize = 0x1C;
// Later tags insert a user data offset before the frames, which moves the
// name that directly follows the header.
constexpr std::size_t TagWithUserDataSize = 0x20;
constexpr std::size_t GroupRefSize = 0x24;
constexpr std::size_t LegacyGroupRefSize = 0x1C;
constexpr std::size_t GroupNameLength = 33;
constexpr std::size_t LegacyGroupNameLength = 24;
constexpr u32 GroupNameVersion = 0x04000000;
constexpr std::size_t ShareHeaderSize = 0x10;
constexpr std::size_t ShareInfoSize = 0x34;
constexpr std::size_t ShareNameLength = ResourceNameLength + 1;
constexpr std::size_t AnimHeaderSize = 0x14;
constexpr std::size_t ContentHeaderSize = MaterialNameLength + 4;
constexpr std::size_t AnimInfoHeaderSize = 8;

struct Kind {
  std::string_view signature;
  std::string_view name;
};

constexpr Kind Kinds[] = {
    {"FLPA", "Pane SRT"},
    {"FLVI", "Visibility"},
    {"FLVC", "Vertex Color"},
    {"FLMC", "Material Color"},
    {"FLTS", "Texture SRT"},
    {"FLTP", "Texture Pattern"},
    {"FLIM", "Indirect SRT"},
    {"FLAC", "Alpha Compare"},
    {"FLFS", "Font Shadow"},
    {"FLCT", "Per-Character Transform"},
    {"FLCC", "Per-Character Transform Curve"},
    {"FLWN", "Window"},
    {"FLEU", "Extended User Data"},
    {"FLMT", "Mask Texture SRT"},
    {"FLDS", "Drop Shadow"},
    {"FLPS", "Procedural Shape"},
    {"FSMA", "State Machine"},
    {"FPSM", "Parts State Machine"},
    {"FVGA", "Vector Graphics"},
    {"FTBR", "Texture Brick Repeat"},
};

std::string kind_name(const ByteReader& r, std::size_t at) {
  const Bytes sig = r.data().subspan(at, 4);
  const std::string_view text(reinterpret_cast<const char*>(sig.data()), sig.size());
  for (const Kind& k : Kinds)
    if (k.signature == text)
      return std::string(k.name);
  return fmt_quoted(sig);
}

std::string content_type(u8 type) {
  switch (type) {
  case 0:
    return "pane";
  case 1:
    return "material";
  case 2:
    return "user data";
  case 3:
    return "state machine";
  case 4:
    return "parts state machine";
  default:
    return "type " + std::to_string(type);
  }
}

void read_tag(const ByteReader& r, const Block& b, u32 version, Section& s, Group& groups, Analysis& out,
              const Context& ctx) {
  r.require(b.offset, TagHeaderSize, "animation tag");
  const u16 order = r.u16_at(b.offset + 8);
  const u16 group_count = r.u16_at(b.offset + 0xA);
  const u32 name_at = r.u32_at(b.offset + 0xC);
  const u32 groups_at = r.u32_at(b.offset + 0x10);
  const std::size_t frames_at = name_at >= TagWithUserDataSize ? 0x18 : 0x14;
  if (name_at < TagHeaderSize || name_at >= b.size)
    throw ParseError("animation tag name at " + fmt_hex(name_at) + " lies outside its block");

  s.add("Tag", r.cstring_at(b.offset + name_at, std::min(MaxNameLength, b.size - name_at)));
  s.add("Tag Order", std::to_string(order));
  s.add("Tag Frames",
        std::to_string(r.s16_at(b.offset + frames_at)) + " to " + std::to_string(r.s16_at(b.offset + frames_at + 2)));
  if (r.u8_at(b.offset + frames_at + 4) & 1)
    s.add("Tag Binding", "Descendants");

  const bool legacy = version < GroupNameVersion;
  const std::size_t stride = legacy ? LegacyGroupRefSize : GroupRefSize;
  if (groups_at > b.size || std::size_t{group_count} * stride > b.size - groups_at) {
    out.errors.push_back("Layout animation tag lists " + std::to_string(group_count) + " groups past its block");
    return;
  }
  const std::size_t name_length = legacy ? LegacyGroupNameLength : GroupNameLength;
  for (u16 i = 0; i < group_count; ++i) {
    ctx.spend(stride, "tag groups");
    groups.rows.push_back({fixed_name(r, b.offset + groups_at + std::size_t{i} * stride, name_length)});
  }
}

void read_shares(const ByteReader& r, const Block& b, Group& shares, Analysis& out, const Context& ctx) {
  r.require(b.offset, ShareHeaderSize, "animation share block");
  const u32 at = r.u32_at(b.offset + 8);
  const u16 count = r.u16_at(b.offset + 0xC);
  if (at > b.size || std::size_t{count} * ShareInfoSize > b.size - at) {
    out.errors.push_back("Layout animation shares " + std::to_string(count) + " entries past its block");
    return;
  }
  for (u16 i = 0; i < count; ++i) {
    const std::size_t e = b.offset + at + std::size_t{i} * ShareInfoSize;
    ctx.spend(ShareInfoSize, "shares");
    shares.rows.push_back({fixed_name(r, e, ShareNameLength) + " with group " +
                           fixed_name(r, e + ShareNameLength, ShareNameLength)});
  }
}

void read_animation(const ByteReader& r, const Block& b, Section& s, Group& textures, Group& targets,
                    Analysis& out, const Context& ctx) {
  r.require(b.offset, AnimHeaderSize, "animation block");
  const u16 frames = r.u16_at(b.offset + 8);
  const u8 loop = r.u8_at(b.offset + 0xA);
  const u16 file_count = r.u16_at(b.offset + 0xC);
  const u16 content_count = r.u16_at(b.offset + 0xE);
  const u32 contents_at = r.u32_at(b.offset + 0x10);
  const std::size_t end = b.offset + b.size;
  s.add("Frames", std::to_string(frames));
  s.add("Loop", fmt_bool(loop != 0));

  for (std::string& n : offset_names(r, b.offset + AnimHeaderSize, file_count, end, "Textures", ctx))
    textures.rows.push_back({std::move(n)});

  if (contents_at > b.size || std::size_t{content_count} * 4 > b.size - contents_at) {
    out.errors.push_back("Layout animation lists " + std::to_string(content_count) + " targets past its block");
    return;
  }
  for (u16 i = 0; i < content_count; ++i) {
    const u32 c = r.u32_at(b.offset + contents_at + std::size_t{i} * 4);
    if (c > b.size || ContentHeaderSize > b.size - c) {
      out.errors.push_back("Layout animation target " + std::to_string(i) + " at " + fmt_hex(c) +
                           " lies outside its block");
      continue;
    }
    const std::size_t at = b.offset + c;
    const u8 info_count = r.u8_at(at + MaterialNameLength);
    const u8 type = r.u8_at(at + MaterialNameLength + 1);
    ctx.spend(ContentHeaderSize + std::size_t{info_count} * 4, "targets");
    std::vector<std::string> kinds;
    for (u8 j = 0; j < info_count; ++j) {
      const u64 info = u64{c} + r.u32_at(at + ContentHeaderSize + std::size_t{j} * 4);
      if (info > b.size || AnimInfoHeaderSize > b.size - info) {
        out.errors.push_back("Layout animation target " + std::to_string(i) + " has an animation outside its block");
        continue;
      }
      kinds.push_back(kind_name(r, b.offset + static_cast<std::size_t>(info)));
    }
    targets.rows.push_back(
        {fixed_name(r, at, MaterialNameLength) + " (" + content_type(type) + "): " + join(kinds)});
  }
}

}  // namespace

bool probe(Bytes data) {
  return layout::probe(data, "FLAN");
}

void analyze(Bytes data, Analysis& out, const Context& ctx) {
  ByteReader r(data, Endian::Big);
  Section& s = out.section(std::string(Name));
  const FileHeader h = read_header(r, Name, out, s, data.size());
  const std::vector<Block> blocks = read_blocks(r, h, "Layout animation", out, ctx);

  Group groups{"Tag Groups"};
  Group shares{"Shared Animations"};
  Group textures{"Textures"};
  Group targets{"Targets"};
  for (const Block& b : blocks) {
    if (b.kind == "pat1")
      read_tag(r, b, h.version, s, groups, out, ctx);
    else if (b.kind == "pah1")
      read_shares(r, b, shares, out, ctx);
    else if (b.kind == "pai1")
      read_animation(r, b, s, textures, targets, out, ctx);
    else
      out.warnings.push_back("Layout animation block " + fmt_quoted(r.data().subspan(b.offset, 4)) + " at " +
                             fmt_hex(b.offset) + " is not recognized");
  }
  s.add("Targets", std::to_string(targets.rows.size()));

  add_groups(out, std::string(Name) + " Contents", {groups, shares, textures, targets});
}

namespace {

std::string text_frames(const xml::Element& e) {
  return e.attribute_or("startFrame", "?") + " to " + e.attribute_or("endFrame", "?");
}

// Each lan element animates one kind across its targets. Separate-mode
// documents keep them inside their tags instead of the body.
void add_text_lan(const xml::Element& lan, std::string_view tag, Group& animations, Group& targets,
                  std::vector<std::vector<std::string>>& target_kinds, const Context& ctx) {
  const std::string kind = lan.attribute_or("animType", "?");
  const auto contents = lan.children_named("animContent");
  std::string row = kind;
  if (!tag.empty())
    row += " (tag " + std::string(tag) + ")";
  animations.rows.push_back({row + ": frames " + text_frames(lan) + ", " + count_of(contents.size(), "target")});
  for (const xml::Element* c : contents) {
    ctx.spend(ContentHeaderSize, "targets");
    const std::string name = c->attribute_or("name");
    std::size_t i = 0;
    while (i < targets.rows.size() && targets.rows[i].text != name)
      ++i;
    if (i == targets.rows.size()) {
      targets.rows.push_back({name});
      target_kinds.emplace_back();
    }
    if (std::find(target_kinds[i].begin(), target_kinds[i].end(), kind) == target_kinds[i].end())
      target_kinds[i].push_back(kind);
  }
}

}  // namespace

bool probe_text(Bytes data) {
  const std::string kind = text_body_kind(data);
  return kind == "animTag" || kind == "animShare" || kind == "lan";
}

void analyze_text(Bytes data, Analysis& out, const Context& ctx) {
  out.format = std::string(TextName);
  const xml::Element root = parse_text(data, ctx);
  const xml::Element* body = text_body(root);
  if (body == nullptr)
    throw ParseError("no body in the layout animation");

  Section& s = out.section(std::string(TextName));
  add_text_header(root, s);
  if (body->attribute_or("separateMode") == "true")
    s.add("Separate Mode", "Yes");

  Group tags("Tags");
  Group shares("Shared Animations");
  Group animations("Animations");
  Group targets("Targets");
  std::vector<std::vector<std::string>> target_kinds;
  for (const xml::Element* share : body->children_named("animShare"))
    for (const xml::Element* info : share->children_named("animShareInfo"))
      shares.rows.push_back(
          {info->attribute_or("srcPaneName") + " with group " + info->attribute_or("targetGroupName")});
  for (const xml::Element* tag : body->children_named("animTag")) {
    const std::string name = tag->attribute_or("name");
    std::vector<std::string> groups;
    for (const xml::Element* g : tag->children_named("group"))
      groups.push_back(g->attribute_or("name"));
    std::string row = name + ": frames " + text_frames(*tag);
    if (const std::string loop = tag->attribute_or("animLoop"); !loop.empty())
      row += ", " + loop;
    if (!groups.empty())
      row += ", " + count_of(groups.size(), "group") + " (" + join(groups) + ")";
    tags.rows.push_back({std::move(row)});
    for (const xml::Element* lan : tag->children_named("lan"))
      add_text_lan(*lan, name, animations, targets, target_kinds, ctx);
  }
  for (const xml::Element* lan : body->children_named("lan"))
    add_text_lan(*lan, {}, animations, targets, target_kinds, ctx);
  for (std::size_t i = 0; i < targets.rows.size(); ++i)
    targets.rows[i].text += ": " + join(target_kinds[i]);

  s.add("Tags", std::to_string(tags.rows.size()));
  s.add("Animations", std::to_string(animations.rows.size()));
  s.add("Targets", std::to_string(targets.rows.size()));
  add_groups(out, std::string(TextName) + " Contents", {tags, shares, animations, targets});
}

}  // namespace nnfck::flan
