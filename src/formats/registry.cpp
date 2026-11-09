#include <algorithm>
#include <array>

#include "formats/bntx/bntx.hpp"
#include "formats/byml/byml.hpp"
#include "formats/eft/eftb.hpp"
#include "formats/eft/eset.hpp"
#include "formats/format.hpp"
#include "formats/fres/fres.hpp"
#include "formats/fres/intermediate.hpp"
#include "formats/layout/layout.hpp"
#include "formats/sarc/sarc.hpp"
#include "formats/xlink/xlink.hpp"
#include "formats/yaz/yaz.hpp"
#include "formats/zstd/zstd.hpp"

namespace nnfck {

namespace {

// Probes with longer or more specific signatures come first. FLIM's signature
// sits at an offset the file's last bytes give, and BYML's two-byte magic is
// the weakest check, so they run last.
constexpr std::array<FormatHandler, 19> Handlers = {{
    {yaz::Name, library::Sead, yaz::probe, yaz::analyze},
    {zstd::Name, library::Zstandard, zstd::probe, zstd::analyze},
    {sarc::Name, library::Sead, sarc::probe, sarc::analyze},
    {fres::Name, library::NintendoWare, fres::probe, fres::analyze},
    {bntx::Name, library::NintendoSDK, bntx::probe, bntx::analyze},
    {flyt::Name, library::NintendoWare, flyt::probe, flyt::analyze},
    {flan::Name, library::NintendoWare, flan::probe, flan::analyze},
    {fcpx::Name, library::NintendoWare, fcpx::probe, fcpx::analyze},
    {eft::Name, library::NintendoWare, eft::probe, eft::analyze},
    {xlink::Name, library::XLink2, xlink::probe, xlink::analyze},
    {elink::Name, library::EffectLink, elink::probe, elink::analyze},
    {slink::Name, library::SoundLink, slink::probe, slink::analyze},
    {flim::Name, library::NintendoWare, flim::probe, flim::analyze},
    {flyt::TextName, library::NintendoWare, flyt::probe_text, flyt::analyze_text},
    {flan::TextName, library::NintendoWare, flan::probe_text, flan::analyze_text},
    {fcpx::TextName, library::NintendoWare, fcpx::probe_text, fcpx::analyze_text},
    {fres::intermediate::Name, library::NintendoWare, fres::intermediate::probe, fres::intermediate::analyze},
    {eset::Name, library::NintendoWare, eset::probe, eset::analyze},
    {byml::Name, library::Sead, byml::probe, byml::analyze},
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
    if (!out.sections.empty())
      out.sections.front().prepend("Library", std::string(h->library));
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

std::string member_format(Bytes data, const Context& ctx) {
  const Analysis a = analyze(data, ctx.member());
  if (a.format == "Unknown")
    return "Unknown data";
  if (!a.errors.empty())
    return a.format + ", corrupt";
  return a.format;
}

void absorb_payload(Analysis& out, std::string_view compression, Analysis&& inner) {
  std::string what = inner.format;
  if (what == "Unknown")
    what = "unknown data";
  else if (what == "Empty")
    what = "empty data";
  out.format = std::string(compression) + "-compressed " + what;
  out.absorb(std::move(inner));
}

std::string member_header() {
  return "Offset      Size        Name";
}

std::string member_row(u64 offset, u64 size, std::string_view name, std::string_view format) {
  return fmt_hex(offset, 8) + "  " + fmt_hex(size, 8) + "  " + std::string(name) + "  [" + std::string(format) +
         "]";
}

}  // namespace nnfck
