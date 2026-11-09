#include <algorithm>
#include <array>

#include "formats/bfres/bfres.hpp"
#include "formats/byml/byml.hpp"
#include "formats/eft/eftb.hpp"
#include "formats/format.hpp"
#include "formats/havok/packfile.hpp"
#include "formats/sarc/sarc.hpp"
#include "formats/yaz/yaz.hpp"

namespace nnfck {

namespace {

// Probes with longer or more specific signatures come first; BYML's two-byte
// magic is the weakest check, so it runs last.
constexpr std::array<FormatHandler, 6> Handlers = {{
    {yaz::Name, yaz::probe, yaz::analyze},
    {sarc::Name, sarc::probe, sarc::analyze},
    {bfres::Name, bfres::probe, bfres::analyze},
    {havok::Name, havok::probe, havok::analyze},
    {eft::Name, eft::probe, eft::analyze},
    {byml::Name, byml::probe, byml::analyze},
}};

constexpr std::size_t PreviewLength = 16;

}  // namespace

const FormatHandler* identify(Bytes data) {
  for (const FormatHandler& h : Handlers)
    if (h.probe(data))
      return &h;
  return nullptr;
}

Analysis analyze(Bytes data, const Context& ctx) {
  Analysis out;
  if (ctx.depth > Context::MaxDepth) {
    out.format = "Unknown";
    out.errors.push_back("container nesting exceeds " + std::to_string(Context::MaxDepth) + " levels");
    return out;
  }
  if (!ctx.charge(data.size())) {
    out.format = "Not analyzed";
    out.errors.push_back("analysis budget exhausted; " + fmt_hex(data.size()) + " bytes left unanalyzed");
    return out;
  }

  if (const FormatHandler* h = identify(data)) {
    try {
      h->analyze(data, out, ctx);
    } catch (const ParseError& e) {
      if (out.format.empty())
        out.format = std::string(h->name);
      out.errors.push_back(std::string(h->name) + ": " + e.what());
    }
    return out;
  }

  out.format = data.empty() ? "Empty" : "Unknown";
  if (!data.empty()) {
    Section& s = out.section("Unknown Data");
    s.add("Size", fmt_size(data.size()));
    const Bytes head = data.first(std::min(data.size(), PreviewLength));
    s.add("First Bytes", fmt_hex_bytes(head));
    s.add("As Text", fmt_quoted(head));
  }
  return out;
}

}  // namespace nnfck
