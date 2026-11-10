#include "core/json.hpp"

#include <charconv>

#include "core/error.hpp"
#include "core/utf8.hpp"

namespace nnfck::json {

const Value* Value::member(std::string_view key) const {
  if (kind != Kind::Object)
    return nullptr;
  for (std::size_t i = 0; i < keys.size(); ++i)
    if (keys[i] == key)
      return &items[i];
  return nullptr;
}

namespace {

class Parser {
public:
  explicit Parser(Bytes data) : m_text(reinterpret_cast<const char*>(data.data()), data.size()) {
    if (m_text.starts_with("\xEF\xBB\xBF"))
      m_pos = 3;
  }

  Value document() {
    Value v = value(0);
    skip_space();
    if (m_pos != m_text.size())
      fail("content after the value");
    return v;
  }

private:
  std::string_view m_text;
  std::size_t m_pos = 0;

  [[noreturn]] void fail(std::string_view what) const {
    throw ParseError("JSON " + std::string(what) + " at offset " + ByteReader::hex_offset(m_pos));
  }

  void skip_space() {
    while (m_pos < m_text.size() &&
           (m_text[m_pos] == ' ' || m_text[m_pos] == '\t' || m_text[m_pos] == '\n' || m_text[m_pos] == '\r'))
      ++m_pos;
  }

  bool consume(std::string_view s) {
    if (!m_text.substr(m_pos).starts_with(s))
      return false;
    m_pos += s.size();
    return true;
  }

  void expect(char c) {
    skip_space();
    if (m_pos >= m_text.size() || m_text[m_pos] != c)
      fail(std::string("expected '") + c + "'");
    ++m_pos;
  }

  Value value(int depth) {
    if (depth > MaxDepth)
      fail("nests deeper than " + std::to_string(MaxDepth) + " levels");
    skip_space();
    if (m_pos >= m_text.size())
      fail("expected a value");
    const char c = m_text[m_pos];
    Value v;
    if (c == '{') {
      v.kind = Value::Kind::Object;
      object(v, depth);
    } else if (c == '[') {
      v.kind = Value::Kind::Array;
      array(v, depth);
    } else if (c == '"') {
      v.kind = Value::Kind::String;
      v.string = string();
    } else if (consume("true")) {
      v.kind = Value::Kind::Bool;
      v.boolean = true;
    } else if (consume("false")) {
      v.kind = Value::Kind::Bool;
    } else if (!consume("null")) {
      v.kind = Value::Kind::Number;
      v.number = number();
    }
    return v;
  }

  void object(Value& o, int depth) {
    ++m_pos;
    skip_space();
    if (consume("}"))
      return;
    for (;;) {
      skip_space();
      if (m_pos >= m_text.size() || m_text[m_pos] != '"')
        fail("expected a member name");
      o.keys.push_back(string());
      expect(':');
      o.items.push_back(value(depth + 1));
      skip_space();
      if (consume("}"))
        return;
      expect(',');
    }
  }

  void array(Value& a, int depth) {
    ++m_pos;
    skip_space();
    if (consume("]"))
      return;
    for (;;) {
      a.items.push_back(value(depth + 1));
      skip_space();
      if (consume("]"))
        return;
      expect(',');
    }
  }

  u32 hex4() {
    if (m_text.size() - m_pos < 4)
      fail("truncated \\u escape");
    u32 v = 0;
    const auto [end, ec] = std::from_chars(m_text.data() + m_pos, m_text.data() + m_pos + 4, v, 16);
    if (ec != std::errc() || end != m_text.data() + m_pos + 4)
      fail("malformed \\u escape");
    m_pos += 4;
    return v;
  }

  std::string string() {
    ++m_pos;
    std::string out;
    for (;;) {
      if (m_pos >= m_text.size())
        fail("unterminated string");
      const char c = m_text[m_pos++];
      if (c == '"')
        return out;
      if (static_cast<unsigned char>(c) < 0x20)
        fail("control character in string");
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (m_pos >= m_text.size())
        fail("unterminated escape");
      switch (m_text[m_pos++]) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u': {
        u32 cp = hex4();
        if (cp >= 0xD800 && cp < 0xDC00) {
          if (!consume("\\u"))
            fail("unpaired surrogate");
          const u32 low = hex4();
          if (low < 0xDC00 || low >= 0xE000)
            fail("unpaired surrogate");
          cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        } else if (cp >= 0xDC00 && cp < 0xE000) {
          fail("unpaired surrogate");
        }
        append_utf8(out, cp);
        break;
      }
      default: fail("unknown escape");
      }
    }
  }

  double number() {
    const std::size_t start = m_pos;
    if (m_pos < m_text.size() && m_text[m_pos] == '-')
      ++m_pos;
    while (m_pos < m_text.size() && ((m_text[m_pos] >= '0' && m_text[m_pos] <= '9') || m_text[m_pos] == '.' ||
                                     m_text[m_pos] == 'e' || m_text[m_pos] == 'E' || m_text[m_pos] == '+' ||
                                     m_text[m_pos] == '-'))
      ++m_pos;
    double v = 0;
    const auto [end, ec] = std::from_chars(m_text.data() + start, m_text.data() + m_pos, v);
    if (m_pos == start || ec != std::errc() || end != m_text.data() + m_pos)
      fail("expected a value");
    return v;
  }
};

}  // namespace

Value parse(Bytes data) {
  return Parser(data).document();
}

}  // namespace nnfck::json
