#include "core/file.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <system_error>

namespace nnfck {

std::vector<u8> read_file(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec))
    throw std::runtime_error("is a directory");

  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error(std::strerror(errno));

  in.seekg(0, std::ios::end);
  const std::streamoff length = in.tellg();
  if (length < 0)
    throw std::runtime_error("unable to determine file size");
  in.seekg(0, std::ios::beg);

  std::vector<u8> data(static_cast<std::size_t>(length));
  if (!data.empty() && !in.read(reinterpret_cast<char*>(data.data()), length))
    throw std::runtime_error("read failed");
  return data;
}

}  // namespace nnfck
