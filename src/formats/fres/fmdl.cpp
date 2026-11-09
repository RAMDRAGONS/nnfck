#include "formats/fres/resources.hpp"

// Models: a skeleton, shapes and materials over shared vertex buffers.
namespace nnfck::fres::fmdl {

namespace {

constexpr std::size_t CafeVertexStride = 0x20;

}  // namespace

std::string describe_cafe(const ByteReader& r, Analysis& out, std::size_t at, const std::string& name,
                          const Context& ctx) {
  const u16 vertex_count = r.u16_at(at + 0x20);
  const u16 shape_count = r.u16_at(at + 0x22);
  const u16 material_count = r.u16_at(at + 0x24);
  const std::string owner = "model \"" + name + "\"";
  expect_count(out, owner, "shapes", shape_count, cafe_dict_count(r, at + 0x14), "index group");
  expect_count(out, owner, "materials", material_count, cafe_dict_count(r, at + 0x18), "index group");

  std::vector<std::string> parts;
  const u64 skeleton = cafe_offset(r, at + 0x0C);
  if (skeleton != 0 && r.magic_at(static_cast<std::size_t>(skeleton), "FSKL"))
    parts.push_back(count_of(r.u16_at(static_cast<std::size_t>(skeleton) + 0x08), "bone"));
  else
    out.warnings.push_back("BFRES " + owner + " has no FSKL skeleton block");
  parts.push_back(count_of(shape_count, "shape"));
  parts.push_back(count_of(material_count, "material"));

  const u64 vertices = cafe_offset(r, at + 0x10);
  // Models may share one vertex buffer array, so each walk is charged.
  ctx.spend(u64{vertex_count} * CafeVertexStride, "vertex buffers");
  u64 total = 0;
  for (u16 i = 0; i < vertex_count && vertices != 0; ++i) {
    const std::size_t v = static_cast<std::size_t>(vertices) + std::size_t(i) * CafeVertexStride;
    if (!r.magic_at(v, "FVTX")) {
      out.warnings.push_back("BFRES " + owner + " vertex buffer " + std::to_string(i) + " lacks its FVTX signature");
      break;
    }
    total += r.u32_at(v + 0x08);
  }
  parts.push_back(count_of(vertex_count, "vertex buffer"));
  parts.push_back(count_of(total, "vertex", "vertices"));
  return join(parts);
}

std::string describe_nx(const ByteReader& r, const NxLayout& l, Analysis& out, std::size_t at,
                        const std::string& name, const Context& ctx) {
  const u16 vertex_count = r.u16_at(at + l.model_vertex_count);
  const u16 shape_count = r.u16_at(at + l.model_shape_count);
  const u16 material_count = r.u16_at(at + l.model_material_count);
  const std::string owner = "model \"" + name + "\"";
  expect_count(out, owner, "shapes", shape_count, nx_dict_count(r, at + l.model_shape_dic), "dictionary");
  expect_count(out, owner, "materials", material_count, nx_dict_count(r, at + l.model_material_dic), "dictionary");

  std::vector<std::string> parts;
  const u64 skeleton = r.u64_at(at + l.model_skeleton);
  if (skeleton != 0 && r.magic_at(static_cast<std::size_t>(skeleton), "FSKL"))
    parts.push_back(count_of(r.u16_at(static_cast<std::size_t>(skeleton) + l.skeleton_bone_count), "bone"));
  else
    out.warnings.push_back("BFRES " + owner + " has no FSKL skeleton block");
  parts.push_back(count_of(shape_count, "shape"));
  parts.push_back(count_of(material_count, "material"));

  const u64 vertices = r.u64_at(at + l.model_vertices);
  // Models may share one vertex buffer array, so each walk is charged.
  ctx.spend(u64{vertex_count} * l.vertex_stride, "vertex buffers");
  u64 total = 0;
  for (u16 i = 0; i < vertex_count && vertices != 0; ++i) {
    const std::size_t v = static_cast<std::size_t>(vertices) + std::size_t(i) * l.vertex_stride;
    if (!r.magic_at(v, "FVTX")) {
      out.warnings.push_back("BFRES " + owner + " vertex buffer " + std::to_string(i) + " lacks its FVTX signature");
      break;
    }
    total += r.u32_at(v + l.vertex_count);
  }
  parts.push_back(count_of(vertex_count, "vertex buffer"));
  parts.push_back(count_of(total, "vertex", "vertices"));
  return join(parts);
}

}  // namespace nnfck::fres::fmdl
