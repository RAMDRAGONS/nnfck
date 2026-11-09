#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/xml.hpp"
#include "formats/layout/layout.hpp"
#include "formats/listing.hpp"

// Layout, layout animation and complex font files share one file header:
// signature, byte order mark, header size, version, file size and block count.
namespace nnfck::layout {

inline constexpr std::size_t FileHeaderSize = 0x14;
inline constexpr std::size_t BlockHeaderSize = 0x8;
inline constexpr std::size_t ResourceNameLength = 24;
inline constexpr std::size_t MaterialNameLength = 28;
inline constexpr std::size_t MaxNameLength = 0x400;

struct FileHeader {
  u16 header_size;
  u32 version;
  u32 file_size;
  u16 block_count;
};

struct Block {
  std::string kind;
  std::size_t offset;
  std::size_t size;
};

bool probe(Bytes data, std::string_view magic);

// Reads the byte order mark into `r` and adds the header rows; the format
// line names the platform the byte order implies. `file_size` is the size the
// header should declare, which differs from `r` when the header is a footer.
FileHeader read_header(ByteReader& r, std::string_view name, Analysis& out, Section& s, std::size_t file_size);

// The block chain that follows the file header. Stops at the first block
// that does not fit and reports it.
std::vector<Block> read_blocks(const ByteReader& r, const FileHeader& h, std::string_view what, Analysis& out,
                               const Context& ctx);

std::string version_string(u32 version);
using listing::count_of;
using listing::join;

// A fixed-width name field, which may fill its width without a terminator.
std::string fixed_name(const ByteReader& r, std::size_t at, std::size_t width);

// Names reached through a table of u32 offsets relative to the table start.
std::vector<std::string> offset_names(const ByteReader& r, std::size_t table, std::size_t count, std::size_t end,
                                      std::string_view what, const Context& ctx);

using listing::Group;
using listing::Row;
using listing::add_groups;

// The XML sources share a root element, told apart by what its body holds.
inline constexpr std::string_view TextRoot = "nw4f_layout";

// The local name of the first element in an nw4f_layout body, found by
// scanning rather than parsing so probes stay cheap. Empty when absent.
std::string text_body_kind(Bytes data);

// Parses an XML source, charging its size to the budget first.
xml::Element parse_text(Bytes data, const Context& ctx);

// Adds the version and the generator and creation details from the head.
void add_text_header(const xml::Element& root, Section& s);

// The body of an nw4f_layout document, or null.
const xml::Element* text_body(const xml::Element& root);

}  // namespace nnfck::layout
