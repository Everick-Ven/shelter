// SHELTER — shared path guards for profile deletion and extension archives.
#ifndef SHELTER_SECURITY_PATHS_H_
#define SHELTER_SECURITY_PATHS_H_

#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace shelter {
namespace security {

// A persisted profile name is exactly one portable path component. It is not
// enough to remove ".." after joining: reject every character outside the
// namespace used by SanitizeForPath before any filesystem operation.
inline bool IsSafeProfileId(const std::string& id) {
  if (id.empty() || id.size() > 80) return false;
  for (unsigned char c : id) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_')) {
      return false;
    }
  }
  return true;
}

inline bool IsReservedWindowsDeviceName(const std::string& component) {
  const std::size_t dot = component.find('.');
  std::string stem = component.substr(0, dot);
  for (char& c : stem) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
      stem == "clock$") {
    return true;
  }
  if (stem.size() == 4 &&
      (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) &&
      stem[3] >= '1' && stem[3] <= '9') {
    return true;
  }
  return false;
}

// Normalize ZIP separators and reject names that are absolute, drive-rooted,
// traversal-capable, ambiguous on Windows, or invalid as portable file names.
// Directory entries may have one trailing slash.
inline bool NormalizeSafeZipEntryName(std::string* name) {
  if (!name || name->empty() || name->size() > 4096) return false;
  for (char& ch : *name) {
    if (ch == '\\') ch = '/';
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c == 0x7f || ch == ':') return false;
  }
  if (name->front() == '/') return false;

  std::size_t start = 0;
  while (start < name->size()) {
    const std::size_t slash = name->find('/', start);
    const std::size_t end = slash == std::string::npos ? name->size() : slash;
    if (end == start) return false;  // Reject repeated separators.
    const std::string component = name->substr(start, end - start);
    if (component == "." || component == ".." || component.back() == '.' ||
        component.back() == ' ' || IsReservedWindowsDeviceName(component)) {
      return false;
    }
    if (slash == std::string::npos || slash + 1 == name->size()) break;
    start = slash + 1;
  }
  return true;
}

// Component-wise containment avoids prefix confusion such as Profiles2 being
// treated as a child of Profiles. The candidate must be below (not equal to)
// root.
inline bool IsPathWithin(const std::filesystem::path& root,
                         const std::filesystem::path& candidate) {
  const std::filesystem::path normalized_root = root.lexically_normal();
  const std::filesystem::path normalized_candidate = candidate.lexically_normal();
  auto root_it = normalized_root.begin();
  auto candidate_it = normalized_candidate.begin();
  for (; root_it != normalized_root.end(); ++root_it, ++candidate_it) {
    if (candidate_it == normalized_candidate.end() || *root_it != *candidate_it)
      return false;
  }
  return candidate_it != normalized_candidate.end();
}

// symlink_status alone does not identify every Windows junction/mount point.
// Treat any reparse point as a link so cleanup never descends through it.
inline bool IsLinkOrReparsePoint(const std::filesystem::path& path) {
#if defined(_WIN32)
  const DWORD attributes = GetFileAttributesW(path.c_str());
  // If metadata is unavailable after symlink_status found an entry, fail closed;
  // never descend into a path whose reparse-point status could not be verified.
  return attributes == INVALID_FILE_ATTRIBUTES ||
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(path, ec);
  return ec || std::filesystem::is_symlink(status);
#endif
}

inline bool IsDirectoryWithoutLink(const std::filesystem::path& path) {
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(path, ec);
  return !ec && std::filesystem::is_directory(status) &&
         !IsLinkOrReparsePoint(path);
}

inline bool IsRegularFileWithoutLink(const std::filesystem::path& path) {
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(path, ec);
  return !ec && std::filesystem::is_regular_file(status) &&
         !IsLinkOrReparsePoint(path);
}

inline bool EnsureDirectoryWithoutLink(const std::filesystem::path& path) {
  std::error_code ec;
  auto status = std::filesystem::symlink_status(path, ec);
  if (ec && ec != std::errc::no_such_file_or_directory) return false;
  if (!ec && status.type() != std::filesystem::file_type::not_found) {
    return std::filesystem::is_directory(status) &&
           !IsLinkOrReparsePoint(path);
  }

  ec.clear();
  std::filesystem::create_directories(path, ec);
  return !ec && IsDirectoryWithoutLink(path);
}

// Best-effort overwrite is allowed only for a regular, non-link file with one
// hard link. Overwriting a multiply-linked file would corrupt its sibling path
// outside the disposable browser profile.
inline void OverwriteFileIfUnshared(
    const std::filesystem::path& path,
    std::uintmax_t max_bytes = 32ull * 1024 * 1024) {
  if (!IsRegularFileWithoutLink(path)) return;
  std::error_code ec;
  const std::uintmax_t links = std::filesystem::hard_link_count(path, ec);
  if (ec || links != 1) return;
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec || size == 0 || size > max_bytes) return;

  std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
  if (!file) return;
  constexpr std::size_t kChunk = 64 * 1024;
  const std::vector<char> zeros(kChunk, 0);
  std::uintmax_t left = size;
  file.seekp(0);
  while (left > 0 && file) {
    const std::size_t n = static_cast<std::size_t>(left < kChunk ? left : kChunk);
    file.write(zeros.data(), static_cast<std::streamsize>(n));
    left -= n;
  }
  file.flush();
}

// Remove a subtree without following symlinks, junctions, or other reparse
// points. Every visited path must remain a strict lexical descendant of the
// trusted boundary. The boundary itself is never removed.
inline bool RemoveTreeWithoutFollowingLinks(
    const std::filesystem::path& boundary,
    const std::filesystem::path& target,
    std::size_t max_depth = 512) {
  namespace fs = std::filesystem;
  if (!IsDirectoryWithoutLink(boundary) || !IsPathWithin(boundary, target))
    return false;

  std::function<bool(const fs::path&, std::size_t)> remove_tree;
  remove_tree = [&](const fs::path& path, std::size_t depth) -> bool {
    if (depth > max_depth || !IsPathWithin(boundary, path)) return false;
    std::error_code ec;
    const fs::file_status status = fs::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory ||
        (!ec && status.type() == fs::file_type::not_found)) {
      return true;
    }
    if (ec) return false;

    // Unlink a link/reparse point itself; never inspect its target.
    if (IsLinkOrReparsePoint(path)) {
      ec.clear();
      fs::remove(path, ec);
      return !ec;
    }

    if (fs::is_directory(status)) {
      std::error_code iter_ec;
      fs::directory_iterator it(path, iter_ec);
      const fs::directory_iterator end;
      if (iter_ec) return false;
      while (it != end) {
        const fs::path child = it->path();
        if (!remove_tree(child, depth + 1)) return false;
        it.increment(iter_ec);
        if (iter_ec) return false;
      }
      ec.clear();
      fs::remove(path, ec);
      return !ec;
    }

    if (fs::is_regular_file(status)) OverwriteFileIfUnshared(path);
    ec.clear();
    fs::remove(path, ec);
    return !ec;
  };

  return remove_tree(target, 0);
}

}  // namespace security
}  // namespace shelter

#endif  // SHELTER_SECURITY_PATHS_H_
