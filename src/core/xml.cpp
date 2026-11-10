#include "core/xml.hpp"

#include "core/utf8.hpp"

namespace nnfck::xml {

std::string_view Element::local_name() const {
  const std::size_t colon = name.find(':');
  return colon == std::string::npos ? std::string_view(name) : std::string_view(name).substr(colon + 1);
}

const std::string* Element::attribute(std::string_view key) const {
  for (const auto& [k, v] : attributes)
    if (k == key)
      return &v;
  return nullptr;
}

std::string Element::attribute_or(std::string_view key, std::string_view fallback) const {
  const std::string* v = attribute(key);
  return v ? *v : std::string(fallback);
}

const Element* Element::child(std::string_view local) const {
  for (const Element& c : children)
    if (c.local_name() == local)
      return &c;
  return nullptr;
}

std::vector<const Element*> Element::children_named(std::string_view local) const {
  std::vector<const Element*> out;
  for (const Element& c : children)
    if (c.local_name() == local)
      out.push_back(&c);
  return out;
}

namespace {

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool is_name_char(char c) {
  return !is_space(c) && c != '=' && c != '>' && c != '/' && c != '<' && c != '"' && c != '\'' && c != '?' &&
         c != '!';
}

class Parser {
public:
  Parser(Bytes data, const Limits& limits)
      : m_text(reinterpret_cast<const char*>(data.data()), data.size()), m_limits(limits) {
    if (m_text.starts_with("\xEF\xBB\xBF"))
      m_pos = 3;
  }

  Element document() {
    skip_misc();
    if (!at("<"))
      fail("expected the root element");
    Element root = element(0);
    skip_misc();
    if (m_pos != m_text.size())
      fail("content after the root element");
    return root;
  }

  // Skips the prolog and returns the root's name, or empty if the prolog is
  // malformed.
  std::string root_name() {
    try {
      skip_misc();
      if (!at("<"))
        return {};
      ++m_pos;
      return name();
    } catch (const ParseError&) {
      return {};
    }
  }

private:
  std::string_view m_text;
  Limits m_limits;
  std::size_t m_pos = 0;
  std::size_t m_nodes = 0;

  [[noreturn]] void fail(std::string_view what) const {
    throw ParseError("XML " + std::string(what) + " at offset " + ByteReader::hex_offset(m_pos));
  }

  bool at(std::string_view s) const { return m_text.substr(m_pos).starts_with(s); }

  void skip_space() {
    while (m_pos < m_text.size() && is_space(m_text[m_pos]))
      ++m_pos;
  }

  void skip_past(std::string_view end, std::string_view what) {
    const std::size_t found = m_text.find(end, m_pos);
    if (found == std::string_view::npos)
      fail("unterminated " + std::string(what));
    m_pos = found + end.size();
  }

  // Bracket-aware skip, since an internal subset can hold '>' inside its
  // declarations.
  void skip_doctype() {
    int depth = 0;
    for (; m_pos < m_text.size(); ++m_pos) {
      const char c = m_text[m_pos];
      if (c == '[')
        ++depth;
      else if (c == ']')
        --depth;
      else if (c == '>' && depth <= 0) {
        ++m_pos;
        return;
      }
    }
    fail("unterminated document type declaration");
  }

  void skip_misc() {
    for (;;) {
      skip_space();
      if (at("<?"))
        skip_past("?>", "processing instruction");
      else if (at("<!--"))
        skip_past("-->", "comment");
      else if (at("<!DOCTYPE"))
        skip_doctype();
      else
        return;
    }
  }

  std::string name() {
    const std::size_t start = m_pos;
    while (m_pos < m_text.size() && is_name_char(m_text[m_pos]))
      ++m_pos;
    if (m_pos == start)
      fail("expected a name");
    return std::string(m_text.substr(start, m_pos - start));
  }

  void count_node() {
    if (++m_nodes > m_limits.max_nodes)
      fail("document has more than " + std::to_string(m_limits.max_nodes) + " nodes");
  }

  // Appends `raw` with its character and entity references resolved.
  void decode(std::string& out, std::string_view raw) const {
    for (std::size_t i = 0; i < raw.size(); ++i) {
      if (raw[i] != '&') {
        out.push_back(raw[i]);
        continue;
      }
      const std::size_t semi = raw.find(';', i);
      if (semi == std::string_view::npos)
        fail("unterminated reference");
      const std::string_view ref = raw.substr(i + 1, semi - i - 1);
      if (ref == "lt")
        out.push_back('<');
      else if (ref == "gt")
        out.push_back('>');
      else if (ref == "amp")
        out.push_back('&');
      else if (ref == "quot")
        out.push_back('"');
      else if (ref == "apos")
        out.push_back('\'');
      else if (ref.starts_with('#')) {
        const bool hex = ref.size() > 1 && (ref[1] == 'x' || ref[1] == 'X');
        const std::string_view digits = ref.substr(hex ? 2 : 1);
        if (digits.empty() || digits.size() > 8)
          fail("malformed character reference");
        u32 cp = 0;
        for (const char c : digits) {
          u32 d;
          if (c >= '0' && c <= '9')
            d = static_cast<u32>(c - '0');
          else if (hex && c >= 'a' && c <= 'f')
            d = static_cast<u32>(c - 'a' + 10);
          else if (hex && c >= 'A' && c <= 'F')
            d = static_cast<u32>(c - 'A' + 10);
          else
            fail("malformed character reference");
          cp = cp * (hex ? 16 : 10) + d;
        }
        if (cp > 0x10FFFF)
          fail("character reference out of range");
        append_utf8(out, cp);
      } else {
        fail("unknown entity reference");
      }
      i = semi;
    }
  }

  Element element(int depth) {
    if (depth > m_limits.max_depth)
      fail("elements nest deeper than " + std::to_string(m_limits.max_depth) + " levels");
    count_node();
    ++m_pos;
    Element e;
    e.name = name();
    for (;;) {
      skip_space();
      if (at("/>")) {
        m_pos += 2;
        return e;
      }
      if (at(">")) {
        ++m_pos;
        break;
      }
      std::string key = name();
      skip_space();
      if (!at("="))
        fail("expected '=' after attribute " + key);
      ++m_pos;
      skip_space();
      if (m_pos >= m_text.size() || (m_text[m_pos] != '"' && m_text[m_pos] != '\''))
        fail("expected a quoted attribute value");
      const char quote = m_text[m_pos++];
      const std::size_t end = m_text.find(quote, m_pos);
      if (end == std::string_view::npos)
        fail("unterminated attribute value");
      count_node();
      std::string value;
      decode(value, m_text.substr(m_pos, end - m_pos));
      m_pos = end + 1;
      e.attributes.emplace_back(std::move(key), std::move(value));
    }

    for (;;) {
      if (m_pos >= m_text.size())
        fail("unterminated element " + e.name);
      if (at("</")) {
        m_pos += 2;
        if (name() != e.name)
          fail("mismatched end tag for " + e.name);
        skip_space();
        if (!at(">"))
          fail("expected '>'");
        ++m_pos;
        return e;
      }
      if (at("<!--")) {
        skip_past("-->", "comment");
      } else if (at("<![CDATA[")) {
        const std::size_t start = m_pos + 9;
        skip_past("]]>", "CDATA section");
        e.text.append(m_text.substr(start, m_pos - 3 - start));
      } else if (at("<?")) {
        skip_past("?>", "processing instruction");
      } else if (at("<")) {
        e.children.push_back(element(depth + 1));
      } else {
        const std::size_t end = std::min(m_text.find('<', m_pos), m_text.size());
        decode(e.text, m_text.substr(m_pos, end - m_pos));
        m_pos = end;
      }
    }
  }
};

}  // namespace

Element parse(Bytes data, const Limits& limits) {
  return Parser(data, limits).document();
}

std::string root_name(Bytes data) {
  return Parser(data, {}).root_name();
}

}  // namespace nnfck::xml
