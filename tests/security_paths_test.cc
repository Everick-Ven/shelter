#include "src/security_paths.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#if defined(_WIN32)
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <winioctl.h>
#ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
#define SYMBOLIC_LINK_FLAG_DIRECTORY 0x1
#endif
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
#endif

namespace fs = std::filesystem;

namespace {

int failures = 0;

#if defined(_WIN32)
struct MountPointReparseBuffer {
  DWORD tag;
  WORD data_length;
  WORD reserved;
  WORD substitute_name_offset;
  WORD substitute_name_length;
  WORD print_name_offset;
  WORD print_name_length;
  WCHAR path_buffer[1];
};

bool CreateTestSymlink(const fs::path& link, const fs::path& target,
                       bool directory) {
  DWORD flags = directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
  flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
  if (CreateSymbolicLinkW(link.c_str(), target.c_str(), flags)) return true;
  if (GetLastError() != ERROR_INVALID_PARAMETER) return false;
  flags &= ~SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
  return CreateSymbolicLinkW(link.c_str(), target.c_str(), flags) != FALSE;
}

bool CreateTestJunction(const fs::path& junction, const fs::path& target) {
  if (!CreateDirectoryW(junction.c_str(), nullptr)) return false;
  const std::wstring substitute = L"\\??\\" + target.native();
  const std::wstring print_name = target.native();
  const std::size_t substitute_bytes = substitute.size() * sizeof(WCHAR);
  const std::size_t print_bytes = print_name.size() * sizeof(WCHAR);
  const std::size_t path_bytes = substitute_bytes + sizeof(WCHAR) +
                                 print_bytes + sizeof(WCHAR);
  const std::size_t buffer_size =
      offsetof(MountPointReparseBuffer, path_buffer) + path_bytes;
  if (buffer_size > 16u * 1024u || 8 + path_bytes > 0xffffu) {
    RemoveDirectoryW(junction.c_str());
    return false;
  }

  std::vector<std::uint8_t> storage(buffer_size, 0);
  auto* data = reinterpret_cast<MountPointReparseBuffer*>(storage.data());
  data->tag = IO_REPARSE_TAG_MOUNT_POINT;
  data->data_length = static_cast<WORD>(8 + path_bytes);
  data->substitute_name_offset = 0;
  data->substitute_name_length = static_cast<WORD>(substitute_bytes);
  data->print_name_offset = static_cast<WORD>(substitute_bytes + sizeof(WCHAR));
  data->print_name_length = static_cast<WORD>(print_bytes);
  std::memcpy(data->path_buffer, substitute.data(), substitute_bytes);
  std::memcpy(reinterpret_cast<std::uint8_t*>(data->path_buffer) +
                  data->print_name_offset,
              print_name.data(), print_bytes);

  HANDLE handle = CreateFileW(
      junction.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    RemoveDirectoryW(junction.c_str());
    return false;
  }
  DWORD returned = 0;
  const BOOL ok = DeviceIoControl(
      handle, FSCTL_SET_REPARSE_POINT, data, static_cast<DWORD>(storage.size()),
      nullptr, 0, &returned, nullptr);
  CloseHandle(handle);
  if (!ok) RemoveDirectoryW(junction.c_str());
  return ok != FALSE;
}
#endif

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
  using shelter::security::IsRegularFileWithoutLinkChain;
  using shelter::security::IsRegularFileWithoutSymlinkOrJunctionChain;
  using shelter::security::IsSafeProfileId;
  using shelter::security::IsSymlinkOrJunction;
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
  Check(IsRegularFileWithoutLinkChain(private_file),
        "regular file without linked ancestors accepted");
  Check(IsRegularFileWithoutSymlinkOrJunctionChain(private_file),
        "regular download without symlink or junction accepted");
  Check(!IsRegularFileWithoutLinkChain(profile),
        "directory is not accepted as a file");
  Check(!IsRegularFileWithoutLinkChain(profile / ".." / "outside" / "keep.txt"),
        "parent traversal in file path rejected");

  std::error_code ec;
  const fs::path hard_link = profile / "shared-outside-file.txt";
  fs::create_hard_link(outside_file, hard_link, ec);
  if (ec) {
    std::cerr << "NOTE: hard-link test skipped: " << ec.message() << '\n';
    ec.clear();
  }

  const fs::path link = profile / "outside-link";
#if defined(_WIN32)
  const bool directory_link_created = CreateTestSymlink(link, outside, true);
  Check(directory_link_created, "Windows directory symlink fixture created");
  if (directory_link_created) {
#else
  fs::create_directory_symlink(outside, link, ec);
  if (!ec) {
#endif
    Check(!IsDirectoryWithoutLink(link), "nested profile symlink rejected");
    Check(!IsRegularFileWithoutLinkChain(link / "keep.txt"),
          "symlink in file path rejected");
    Check(!IsRegularFileWithoutSymlinkOrJunctionChain(link / "keep.txt"),
          "symlinked download path rejected");
#if !defined(_WIN32)
  } else {
    std::cerr << "NOTE: symlink test skipped: " << ec.message() << '\n';
    ec.clear();
  }
#endif

  const fs::path file_link = profile / "outside-file-link";
#if defined(_WIN32)
  const bool file_link_created = CreateTestSymlink(file_link, outside_file, false);
  Check(file_link_created, "Windows file symlink fixture created");
  if (file_link_created) {
#else
  fs::create_symlink(outside_file, file_link, ec);
  if (!ec) {
#endif
    Check(!IsRegularFileWithoutLinkChain(file_link),
          "file symlink rejected");
    Check(IsSymlinkOrJunction(file_link), "file symlink tag identified");
    Check(!IsRegularFileWithoutSymlinkOrJunctionChain(file_link),
          "symlinked download file rejected");
#if !defined(_WIN32)
  } else {
    std::cerr << "NOTE: file symlink test skipped: " << ec.message() << '\n';
    ec.clear();
  }
#endif

#if defined(_WIN32)
  const fs::path junction = profile / "outside-junction";
  const bool junction_created = CreateTestJunction(junction, outside);
  Check(junction_created, "Windows junction fixture created");
  if (junction_created) {
    Check(IsSymlinkOrJunction(junction), "junction reparse tag identified");
    Check(!IsRegularFileWithoutSymlinkOrJunctionChain(junction / "keep.txt"),
          "junction in file path rejected");
    Check(!IsRegularFileWithoutLinkChain(junction / "keep.txt"),
          "strict cleanup guard still rejects junction");
    RemoveDirectoryW(junction.c_str());
  }
#endif

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
