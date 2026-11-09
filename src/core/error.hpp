#pragma once

#include <stdexcept>
#include <string>

namespace nnfck {

class ParseError : public std::runtime_error {
public:
  explicit ParseError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace nnfck
