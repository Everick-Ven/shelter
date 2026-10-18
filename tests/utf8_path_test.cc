#include "src/path_utils.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

}  // анонимное пространство имён

int main() {
  std::error_code ec;
  const fs::path temporary = fs::temp_directory_path(ec);
  Check(!ec && !temporary.empty(), "temporary directory is available");
  if (ec || temporary.empty()) return 1;

  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::string russian_test =
      "SHELTER-\xD1\x82\xD0\xB5\xD1\x81\xD1\x82-" + std::to_string(nonce);
  const std::string russian_folder =
      "\xD0\xBF\xD0\xB0\xD0\xBF\xD0\xBA\xD0\xB0";
  const std::string russian_file =
      "\xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB.txt";
  const fs::path root = temporary / shelter::path_utils::PathFromUtf8(russian_test);
  const fs::path directory = root / shelter::path_utils::PathFromUtf8(russian_folder);
  const fs::path file_path = directory / shelter::path_utils::PathFromUtf8(russian_file);

  fs::create_directories(directory, ec);
  Check(!ec && fs::is_directory(directory), "directory with a Cyrillic name is created");
  if (ec) return 1;

  const std::string contents = "UTF-8 path round trip";
  {
    std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
    Check(static_cast<bool>(output), "file inside the Cyrillic directory opens for writing");
    if (output) output << contents;
    output.close();
    Check(!output.fail(), "file inside the Cyrillic directory is written");
  }

  const std::string utf8_path = shelter::path_utils::PathToUtf8(file_path);
  const fs::path round_trip = shelter::path_utils::PathFromUtf8(utf8_path);
  Check(round_trip.lexically_normal() == file_path.lexically_normal(),
        "filesystem paths round-trip through UTF-8");
  {
    std::ifstream input(round_trip, std::ios::binary);
    std::string actual;
    if (input) std::getline(input, actual, '\0');
    Check(input.good() || input.eof(), "file inside the Cyrillic directory opens for reading");
    Check(actual == contents, "file inside the Cyrillic directory is read intact");
  }

  ec.clear();
  fs::remove_all(root, ec);
  Check(!ec, "temporary Cyrillic directory is removed");
  if (failures) return 1;
  std::cout << "utf8_path_test: all checks passed\n";
  return 0;
}
