#include "src/security_paths.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void Check(bool condition, const char* label) {
  if (condition) return;
  std::cerr << "FAIL: " << label << '\n';
  ++failures;
}

}  // namespace

int main() {
  using shelter::security::EnsureDirectoryWithoutLink;
  using shelter::security::IsDirectoryWithoutLink;
  using shelter::security::IsPathWithin;
  using shelter::security::IsSafeProfileId;
  using shelter::security::NormalizeSafeZipEntryName;
  using shelter::security::RemoveTreeWithoutFollowingLinks;

  Check(IsSafeProfileId("space_01-aZ9"), "valid profile ID");
  Check(!IsSafeProfileId(""), "empty profile ID rejected");
  Check(!IsSafeProfileId("../outside"), "profile traversal rejected");
  Check(!IsSafeProfileId("C:\\outside"), "drive path rejected as profile ID");
  Check(!IsSafeProfileId(std::string(81, 'a')), "oversized profile ID rejected");

  auto safe_zip = [](std::string name) {
    return NormalizeSafeZipEntryName(&name);
  };
  Check(safe_zip("manifest.json"), "manifest entry accepted");
  Check(safe_zip("assets/icon-128.png"), "nested relative entry accepted");
  Check(safe_zip("assets\\icon-128.png"), "Windows separators normalized");
  Check(safe_zip("assets/"), "directory entry accepted");
  Check(!safe_zip("/absolute/file"), "POSIX absolute path rejected");
  Check(!safe_zip("\\\\server\\share\\file"), "UNC path rejected");
  Check(!safe_zip("C:/outside/file"), "drive-root ZIP path rejected");
  Check(!safe_zip("C:relative-file"), "drive-relative ZIP path rejected");
  Check(!safe_zip("../outside"), "parent traversal rejected");
  Check(!safe_zip("assets/../outside"), "nested traversal rejected");
  Check(!safe_zip("assets\\..\\outside"), "backslash traversal rejected");
  Check(!safe_zip("assets//file"), "repeated separators rejected");
  Check(!safe_zip("./manifest.json"), "dot component rejected");
  Check(!safe_zip("NUL.txt"), "Windows device path rejected");
  Check(!safe_zip("assets/trailing. "), "Windows trailing dot/space rejected");
  Check(!safe_zip("file:stream"), "alternate data stream rejected");

  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path temp = fs::temp_directory_path() /
                        ("shelter-security-test-" + std::to_string(nonce));
  const fs::path profiles = temp / "Profiles";
  const fs::path profile = profiles / "profile_a";
  const fs::path outside = temp / "outside";
  Check(EnsureDirectoryWithoutLink(profiles), "safe profile root created");
  Check(EnsureDirectoryWithoutLink(profile), "safe profile directory created");
  Check(EnsureDirectoryWithoutLink(outside), "outside test directory created");
  Check(IsPathWithin(profiles, profile), "profile contained by profile root");
  Check(!IsPathWithin(profiles, profiles), "profile root is not a child profile");
  Check(!IsPathWithin(profiles, profiles.parent_path() / "Profiles2" / "profile_a"),
        "sibling prefix is not containment");
  Check(!IsPathWithin(profiles, outside), "outside directory is not contained");
  Check(!RemoveTreeWithoutFollowingLinks(profiles, outside),
        "outside deletion is rejected");

  const fs::path private_file = profile / "private.txt";
  const fs::path outside_file = outside / "keep.txt";
  {
    std::ofstream file(private_file, std::ios::binary);
    file << "profile data";
  }
  {
    std::ofstream file(outside_file, std::ios::binary);
    file << "must not be overwritten or deleted";
  }

  std::error_code ec;
  const fs::path hard_link = profile / "shared-outside-file.txt";
  fs::create_hard_link(outside_file, hard_link, ec);
  if (ec) {
    std::cerr << "NOTE: hard-link test skipped: " << ec.message() << '\n';
    ec.clear();
  }

  const fs::path link = profile / "outside-link";
  fs::create_directory_symlink(outside, link, ec);
  if (!ec) {
    Check(!IsDirectoryWithoutLink(link), "nested profile symlink rejected");
  } else {
    std::cerr << "NOTE: symlink test skipped: " << ec.message() << '\n';
    ec.clear();
  }

  Check(RemoveTreeWithoutFollowingLinks(profiles, profile),
        "profile subtree removed safely");
  Check(!fs::exists(profile), "profile directory removed");
  Check(fs::exists(outside_file), "outside hard-link target still exists");
  std::ifstream preserved(outside_file, std::ios::binary);
  const std::string preserved_text((std::istreambuf_iterator<char>(preserved)),
                                   std::istreambuf_iterator<char>());
  Check(preserved_text == "must not be overwritten or deleted",
        "outside hard-link target contents preserved");
  fs::remove_all(temp, ec);

  if (failures) {
    std::cerr << failures << " security path regression check(s) failed\n";
    return 1;
  }
  std::cout << "SHELTER_SECURITY_PATHS_PASS\n";
  return 0;
}
