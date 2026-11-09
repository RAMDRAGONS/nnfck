#pragma once

#include <string>
#include <string_view>

#include "formats/fres/common.hpp"

// The resource blocks a BFRES holds. Each is a format of its own, with an
// XML intermediate source, but is only ever stored inside a BFRES: Wii U
// blocks name themselves through the BFRES string table and Switch blocks
// address the whole file. Each describe function returns a one-line summary.
namespace nnfck::fres {

namespace fmdl {
inline constexpr std::string_view Name = "Binary Cafe Model";
std::string describe_cafe(const ByteReader& r, Analysis& out, std::size_t at, const std::string& name,
                          const Context& ctx);
std::string describe_nx(const ByteReader& r, const NxLayout& l, Analysis& out, std::size_t at,
                        const std::string& name, const Context& ctx);
}  // namespace fmdl

namespace ftex {
inline constexpr std::string_view Name = "Binary Cafe Texture";
std::string describe_cafe(const ByteReader& r, std::size_t at);
}  // namespace ftex

// Wii U animation blocks before 3.4 (`legacy`) keep 16-bit frame counts and
// narrower element counts.
namespace fska {
inline constexpr std::string_view Name = "Binary Cafe Skeletal Animation";
std::string describe_cafe(const ByteReader& r, std::size_t at, bool legacy);
std::string describe_nx(const ByteReader& r, const NxLayout& l, std::size_t at);
}  // namespace fska

namespace fshu {
inline constexpr std::string_view Name = "Binary Cafe Shader Parameter Animation";
std::string describe_cafe(const ByteReader& r, std::size_t at, bool legacy);
}  // namespace fshu

namespace ftxp {
inline constexpr std::string_view Name = "Binary Cafe Texture Pattern Animation";
std::string describe_cafe(const ByteReader& r, std::size_t at, bool legacy);
}  // namespace ftxp

// Wii U FVIS blocks serve bones or materials, told apart by a flag; Switch
// FBVS blocks serve bones only.
namespace fvis {
inline constexpr std::string_view BoneName = "Binary Cafe Bone Visibility Animation";
inline constexpr std::string_view MaterialName = "Binary Cafe Material Visibility Animation";
std::string describe_cafe(const ByteReader& r, Analysis& out, std::size_t at, bool material,
                          const std::string& name, bool legacy);
std::string describe_nx(const ByteReader& r, const NxLayout& l, std::size_t at);
}  // namespace fvis

namespace fsha {
inline constexpr std::string_view Name = "Binary Cafe Shape Animation";
std::string describe_cafe(const ByteReader& r, std::size_t at, bool legacy);
std::string describe_nx(const ByteReader& r, const NxLayout& l, std::size_t at);
}  // namespace fsha

namespace fscn {
inline constexpr std::string_view Name = "Binary Cafe Scene Animation";
std::string describe_cafe(const ByteReader& r, Analysis& out, std::size_t at, const std::string& name,
                          bool legacy);
std::string describe_nx(const ByteReader& r, const NxLayout& l, std::size_t at);
}  // namespace fscn

namespace fmaa {
inline constexpr std::string_view Name = "Binary Cafe Material Animation";
std::string describe_nx(const ByteReader& r, const NxLayout& l, std::size_t at);
}  // namespace fmaa

}  // namespace nnfck::fres
