#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "formats/format.hpp"
#include "formats/listing.hpp"

namespace nnfck::pmaa {

inline constexpr std::string_view RootName = "param_root";
inline constexpr int MaxDepth = 64;

// Indexed by the type id binary archives store; text archives name the type.
inline constexpr std::string_view ParamTypes[] = {
    "bool",       "f32",       "int",    "vec2", "vec3",       "vec4",          "color",
    "string32",   "string64",  "curve1", "curve2", "curve3",   "curve4",        "buffer_int",
    "buffer_f32", "string256", "quat",   "u32",  "buffer_u32", "buffer_binary", "stringRef",
};
inline constexpr std::size_t ParamTypeCount = std::size(ParamTypes);

inline constexpr u32 BoolType = 0;
inline constexpr u32 F32Type = 1;
inline constexpr u32 IntType = 2;
inline constexpr u32 Curve1Type = 9;
inline constexpr u32 U32Type = 17;
inline constexpr u32 BinaryBufferType = 19;
inline constexpr u32 StringRefType = 20;
inline constexpr std::size_t CurvePoints = 30;

bool is_string(u32 type);
bool is_buffer(u32 type);
bool is_curve(u32 type);
// Component count of float vector types (vec2 to vec4, color, quat), else 0.
std::size_t float_components(u32 type);
// Longest string a fixed-capacity string type holds, excluding its NUL.
std::optional<std::size_t> string_capacity(u32 type);
std::optional<u32> param_type(std::string_view name);

// "Binary Agl Environment (.baglenv)" for a binary archive of extension
// aglenv, "Agl Environment (.aglenv)" for its text form.
std::string extension_label(std::string_view extension, bool binary);

// Totals over the whole tree, plus the root's own lists and objects.
class Contents {
public:
  explicit Contents(Analysis& out) : m_out(out) {}

  u64 lists = 0;
  u64 objects = 0;
  u64 params = 0;

  void list(const std::string& name, int depth, u64 child_lists, u64 child_objects);
  void object(const std::string& name, int depth, u64 child_params);
  void param(u32 type);
  void problem(std::string message);

  // Adds the parameter type totals to `s` and lists the root's members.
  void finish(Section& s, const Context& ctx);

private:
  Analysis& m_out;
  std::array<u64, ParamTypeCount> m_by_type{};
  listing::Group m_lists{"Lists"};
  listing::Group m_objects{"Objects"};
  u64 m_problems = 0;
};

}  // namespace nnfck::pmaa
