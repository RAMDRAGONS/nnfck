#pragma once

#include <string>
#include <string_view>

namespace nnfck {

// Decodes Shift-JIS as extended by Windows code page 932, the encoding of
// Japanese text from Windows-based tools. Bytes that do not form a defined
// character pass through unchanged, so output sanitizing still escapes them.
std::string shift_jis_to_utf8(std::string_view s);

}  // namespace nnfck
