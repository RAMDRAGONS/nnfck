#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/file.hpp"
#include "core/report.hpp"
#include "formats/format.hpp"

namespace {

constexpr int ExitOk = 0;
constexpr int ExitParseError = 1;
constexpr int ExitUsage = 2;

void print_usage(std::ostream& out) {
  out << "Usage: nnfck [options] <file>...\n"
         "\n"
         "Identifies Nintendo file formats and prints their header information.\n"
         "Files are only read; nothing is extracted or modified.\n"
         "\n"
         "Options:\n"
         "  -h, --help     Show this help and exit\n"
         "  -V, --version  Show the version and exit\n"
         "  --             Treat every following argument as a file\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> files;
  bool options_done = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (!options_done && arg == "--") {
      options_done = true;
    } else if (!options_done && (arg == "-h" || arg == "--help")) {
      print_usage(std::cout);
      return ExitOk;
    } else if (!options_done && (arg == "-V" || arg == "--version")) {
      std::cout << "nnfck " << NNFCK_VERSION << '\n';
      return ExitOk;
    } else if (!options_done && arg.size() > 1 && arg[0] == '-') {
      std::cerr << "nnfck: unknown option '" << arg << "'\n\n";
      print_usage(std::cerr);
      return ExitUsage;
    } else {
      files.emplace_back(arg);
    }
  }

  if (files.empty()) {
    print_usage(std::cerr);
    return ExitUsage;
  }

  int status = ExitOk;
  for (std::size_t i = 0; i < files.size(); ++i) {
    if (i != 0)
      std::cout << '\n';

    std::vector<nnfck::u8> data;
    try {
      data = nnfck::read_file(std::filesystem::path(files[i]));
    } catch (const std::exception& e) {
      std::cout << std::flush;
      std::cerr << "nnfck: " << files[i] << ": " << e.what() << '\n';
      status = ExitUsage;
      continue;
    }

    nnfck::Budget budget = nnfck::Budget::for_input(data.size());
    const nnfck::Analysis analysis = nnfck::analyze(nnfck::Bytes(data), nnfck::Context{0, &budget});
    nnfck::print_analysis(std::cout, files[i], analysis);
    if (!analysis.errors.empty() && status == ExitOk)
      status = ExitParseError;
  }
  return status;
}
