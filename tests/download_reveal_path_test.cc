// Проверяет отдельную проверку пути перед «Показать в папке».
#include "src/security_paths.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <cstring>
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

void Check(bool condition, const char* label) {
  if (condition) return;
  std::cerr << "FAIL: " << label << '\n';
  ++failures;
}

#if defined(_WIN32)
bool CreateTestSymlink(const fs::path& link, const fs::path& target,
                       bool directory) {
  DWORD flags = directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
  flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
  if (CreateSymbolicLinkW(link.c_str(), target.c_str(), flags)) return true;
  if (GetLastError() != ERROR_INVALID_PARAMETER) return false;
  flags &= ~SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
  return CreateSymbolicLinkW(link.c_str(), target.c_str(), flags) != FALSE;
}

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

}  // namespace

int main() {
  using shelter::security::IsRegularFileWithoutSymlinkOrJunctionChain;
  using shelter::security::IsSymlinkOrJunction;
#if defined(_WIN32)
  using shelter::security::IsLinkOrReparsePoint;
#endif

  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path temp = fs::temp_directory_path() /
                        ("shelter-download-reveal-" + std::to_string(nonce));
  const fs::path downloads = temp / "Downloads";
  const fs::path outside = temp / "outside";
  std::error_code ec;
  fs::create_directories(downloads, ec);
  Check(!ec, "temporary downloads directory created");
  fs::create_directories(outside, ec);
  Check(!ec, "outside target directory created");

  const fs::path ordinary = downloads / "ordinary.bin";
  const fs::path outside_file = outside / "target.bin";
  {
    std::ofstream file(ordinary, std::ios::binary);
    file << "ordinary download";
  }
  {
    std::ofstream file(outside_file, std::ios::binary);
    file << "external target";
  }
  Check(IsRegularFileWithoutSymlinkOrJunctionChain(ordinary),
        "ordinary download is allowed");

#if defined(_WIN32)
  const fs::path file_link = downloads / "file-symlink";
  const bool file_link_created = CreateTestSymlink(file_link, outside_file, false);
  Check(file_link_created, "Windows file symlink fixture created");
  if (file_link_created) {
    Check(IsSymlinkOrJunction(file_link), "file symlink tag identified");
    Check(!IsRegularFileWithoutSymlinkOrJunctionChain(file_link),
          "file symlink is rejected");
    Check(IsLinkOrReparsePoint(file_link),
          "strict cleanup guard still rejects a symlink");
  }

  const fs::path directory_link = downloads / "directory-symlink";
  const bool directory_link_created =
      CreateTestSymlink(directory_link, outside, true);
  Check(directory_link_created, "Windows directory symlink fixture created");
  if (directory_link_created) {
    Check(!IsRegularFileWithoutSymlinkOrJunctionChain(directory_link / "target.bin"),
          "symlink in parent path is rejected");
  }

  const fs::path junction = downloads / "directory-junction";
  const bool junction_created = CreateTestJunction(junction, outside);
  Check(junction_created, "Windows junction fixture created");
  if (junction_created) {
    Check(IsSymlinkOrJunction(junction), "junction reparse tag identified");
    Check(!IsRegularFileWithoutSymlinkOrJunctionChain(junction / "target.bin"),
          "junction in parent path is rejected");
    Check(IsLinkOrReparsePoint(junction),
          "strict cleanup guard still rejects a junction");
    RemoveDirectoryW(junction.c_str());
  }
#else
  const fs::path file_link = downloads / "file-symlink";
  fs::create_symlink(outside_file, file_link, ec);
  Check(!ec, "file symlink fixture created");
  if (!ec) {
    Check(IsSymlinkOrJunction(file_link), "file symlink identified");
    Check(!IsRegularFileWithoutSymlinkOrJunctionChain(file_link),
          "file symlink is rejected");
  }

  const fs::path directory_link = downloads / "directory-symlink";
  ec.clear();
  fs::create_directory_symlink(outside, directory_link, ec);
  Check(!ec, "directory symlink fixture created");
  if (!ec) {
    Check(!IsRegularFileWithoutSymlinkOrJunctionChain(directory_link / "target.bin"),
          "symlink in parent path is rejected");
  }
#endif

  fs::remove_all(temp, ec);
  Check(!ec, "temporary test files removed");
  if (failures) {
    std::cerr << failures << " download reveal path check(s) failed\n";
    return 1;
  }
  std::cout << "SHELTER_DOWNLOAD_REVEAL_PATH_PASS\n";
  return 0;
}
