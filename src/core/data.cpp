#include "core/data.hpp"

#include <cstdlib>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace nnfck::data {

namespace {

std::optional<std::filesystem::path> executable_path() {
#if defined(_WIN32)
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (n == 0)
      return std::nullopt;
    if (n < buffer.size()) {
      buffer.resize(n);
      return std::filesystem::path(buffer);
    }
    buffer.resize(buffer.size() * 2);
  }
#elif defined(__APPLE__)
  u_int32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0)
    return std::nullopt;
  buffer.resize(buffer.find('\0'));
  std::error_code ec;
  const std::filesystem::path p = std::filesystem::canonical(buffer, ec);
  return ec ? std::nullopt : std::optional(p);
#else
  std::error_code ec;
  const std::filesystem::path p = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::nullopt : std::optional(p);
#endif
}

}  // namespace

std::optional<std::filesystem::path> find(std::string_view file) {
  std::vector<std::filesystem::path> dirs;
  if (const char* env = std::getenv(DirVariable); env != nullptr && *env != '\0')
    dirs.emplace_back(env);
  if (const std::optional<std::filesystem::path> exe = executable_path()) {
    dirs.push_back(exe->parent_path());
    dirs.push_back(exe->parent_path().parent_path() / "share" / "nnfck");
  }
  for (const std::filesystem::path& dir : dirs) {
    std::error_code ec;
    const std::filesystem::path p = dir / file;
    if (std::filesystem::is_regular_file(p, ec))
      return p;
  }
  return std::nullopt;
}

}  // namespace nnfck::data
