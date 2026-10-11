// SHELTER — safely persisted native master key for secret_crypto.cc.
// Windows: DPAPI-protected secret.key. macOS: login Keychain, with an exclusive
// 0600 file fallback/migration. Existing unreadable key material is never replaced.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>

#include "src/platform.h"
#include "src/security_paths.h"

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace shelter {
namespace platform {
namespace {

constexpr size_t kKeyLen = 32;
constexpr std::uintmax_t kMaxPersistedKeyBytes = 64u * 1024u;
constexpr char kKcService[] = "SHELTER";
constexpr char kKcAccount[] = "master-key";

enum class FileState { kMissing, kRegular, kError };

std::once_flag g_key_once;
bool g_key_available = false;
std::string g_key_hex;
std::string g_key_error;

std::string ToHex(const std::string& bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (unsigned char c : bytes) {
    out.push_back(kDigits[c >> 4]);
    out.push_back(kDigits[c & 15]);
  }
  return out;
}

bool IsHexKey(const std::string& value) {
  if (value.size() != kKeyLen * 2) return false;
  for (char c : value) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}

FileState InspectFile(const std::filesystem::path& path) {
  std::error_code ec;
  const std::filesystem::file_status status =
      std::filesystem::symlink_status(path, ec);
  if (ec == std::errc::no_such_file_or_directory ||
      (!ec && status.type() == std::filesystem::file_type::not_found))
    return FileState::kMissing;
  if (ec || !std::filesystem::is_regular_file(status) ||
      security::IsLinkOrReparsePoint(path))
    return FileState::kError;
  return FileState::kRegular;
}

bool ReadAll(const std::filesystem::path& path, std::string* out) {
  if (!out || !security::IsRegularFileWithoutLink(path)) return false;
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec || size > kMaxPersistedKeyBytes) return false;
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  out->assign(static_cast<size_t>(size), '\0');
  if (size && !file.read(out->data(), static_cast<std::streamsize>(size))) {
    out->clear();
    return false;
  }
  return !file.bad();
}

bool WriteAllExclusive(const std::filesystem::path& path,
                       const std::string& data) {
#if defined(_WIN32)
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  size_t offset = 0;
  bool ok = true;
  while (offset < data.size()) {
    const DWORD count = static_cast<DWORD>(std::min<size_t>(
        data.size() - offset, static_cast<size_t>(MAXDWORD)));
    DWORD written = 0;
    if (!WriteFile(file, data.data() + offset, count, &written, nullptr) ||
        written == 0) {
      ok = false;
      break;
    }
    offset += written;
  }
  if (ok && !FlushFileBuffers(file)) ok = false;
  if (!CloseHandle(file)) ok = false;
  if (!ok) DeleteFileW(path.c_str());  // this call created the file exclusively
  return ok && offset == data.size();
#else
  int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  const int fd = ::open(path.c_str(), flags, 0600);
  if (fd < 0) return false;
  size_t offset = 0;
  bool ok = true;
  while (offset < data.size()) {
    const ssize_t written = ::write(fd, data.data() + offset,
                                    data.size() - offset);
    if (written <= 0) {
      ok = false;
      break;
    }
    offset += static_cast<size_t>(written);
  }
  if (ok && ::fsync(fd) != 0) ok = false;
  if (::close(fd) != 0) ok = false;
  if (ok && ::chmod(path.c_str(), 0600) != 0) ok = false;
  if (!ok) ::unlink(path.c_str());  // only the O_EXCL file created above
  return ok && offset == data.size();
#endif
}

#if defined(_WIN32)
bool Protect(const std::string& input, std::string* output) {
  if (!output || input.size() > MAXDWORD) return false;
  DATA_BLOB in{static_cast<DWORD>(input.size()),
               reinterpret_cast<BYTE*>(const_cast<char*>(input.data()))};
  DATA_BLOB encrypted{};
  if (!CryptProtectData(&in, L"SHELTER", nullptr, nullptr, nullptr, 0,
                        &encrypted)) return false;
  output->assign(reinterpret_cast<const char*>(encrypted.pbData),
                 encrypted.cbData);
  LocalFree(encrypted.pbData);
  return true;
}

bool Unprotect(const std::string& input, std::string* output) {
  if (!output || input.empty() || input.size() > MAXDWORD) return false;
  DATA_BLOB in{static_cast<DWORD>(input.size()),
               reinterpret_cast<BYTE*>(const_cast<char*>(input.data()))};
  DATA_BLOB clear{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &clear))
    return false;
  output->assign(reinterpret_cast<const char*>(clear.pbData), clear.cbData);
  if (clear.pbData) SecureZeroMemory(clear.pbData, clear.cbData);
  LocalFree(clear.pbData);
  return true;
}
#else
bool Protect(const std::string& input, std::string* output) {
  if (!output) return false;
  *output = input;
  return true;
}
bool Unprotect(const std::string& input, std::string* output) {
  if (!output) return false;
  *output = input;
  return true;
}
#endif

bool ReadFileKey(const std::filesystem::path& path, std::string* key_hex,
                 std::string* error) {
  std::string raw, key;
  if (!ReadAll(path, &raw)) {
    if (error) *error = "existing master-key file cannot be read";
    return false;
  }
  if (raw.empty() || !Unprotect(raw, &key) || key.size() != kKeyLen) {
    std::fill(key.begin(), key.end(), '\0');
    std::fill(raw.begin(), raw.end(), '\0');
    if (error) *error = "existing master-key file cannot be decoded";
    return false;
  }
  if (key_hex) *key_hex = ToHex(key);
  std::fill(key.begin(), key.end(), '\0');
  std::fill(raw.begin(), raw.end(), '\0');
  return true;
}

bool EnsureUserDataDir(std::filesystem::path* dir, std::string* error) {
  if (!dir) return false;
  *dir = std::filesystem::u8path(UserDataDir());
  if (dir->empty() || !security::EnsureDirectoryWithoutLink(*dir)) {
    if (error) *error = "user data directory is unavailable or linked";
    return false;
  }
  return true;
}

bool NewRandomKey(std::string* key, std::string* error) {
  if (!key || !RandomBytes(kKeyLen, key) || key->size() != kKeyLen) {
    if (error) *error = "operating system random generator failed";
    return false;
  }
  return true;
}

bool StoreFileKeyExclusive(const std::filesystem::path& path,
                           const std::string& key_hex,
                           std::string* error) {
  std::string key(key_hex.size() / 2, '\0');
  for (size_t i = 0; i < key.size(); ++i) {
    const auto digit = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    const int hi = digit(key_hex[i * 2]), lo = digit(key_hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      std::fill(key.begin(), key.end(), '\0');
      if (error) *error = "generated master key is malformed";
      return false;
    }
    key[i] = static_cast<char>((hi << 4) | lo);
  }
  std::string blob;
  const bool protected_ok = Protect(key, &blob);
  std::fill(key.begin(), key.end(), '\0');
  if (!protected_ok || !WriteAllExclusive(path, blob)) {
    std::fill(blob.begin(), blob.end(), '\0');
    if (error) *error = "new master key could not be persisted";
    return false;
  }
  std::fill(blob.begin(), blob.end(), '\0');
  return true;
}

#if defined(__APPLE__)
KeychainReadResult ReadKeychain(const char* service, const char* account,
                                std::string* value) {
  auto output = std::make_shared<std::string>();
  auto result = std::make_shared<KeychainReadResult>(KeychainReadResult::kError);
  const bool completed = RunTimed([=] {
    *result = KeychainGet(service, account, output.get());
    return *result != KeychainReadResult::kError;
  }, 2000);
  if (!completed) return KeychainReadResult::kError;
  if (*result == KeychainReadResult::kFound && value) *value = *output;
  return *result;
}

bool StoreOrReadKeychain(const std::string& proposed, std::string* observed) {
  auto value = std::make_shared<std::string>();
  const bool completed = RunTimed([proposed, value] {
    // Duplicate means another SHELTER process won the first-run race; the
    // read-back below decides which already-persisted key is authoritative.
    KeychainPutOpen(kKcService, kKcAccount, proposed);
    return KeychainGet(kKcService, kKcAccount, value.get()) ==
           KeychainReadResult::kFound;
  }, 2500);
  if (!completed || !IsHexKey(*value)) return false;
  if (observed) *observed = *value;
  return true;
}

bool LoadOrCreateMacKey(const std::filesystem::path& path,
                        std::string* key_hex, std::string* error) {
  std::string kc_value;
  const KeychainReadResult kc = ReadKeychain(kKcService, kKcAccount, &kc_value);
  const FileState file_state = InspectFile(path);
  if (file_state == FileState::kError) {
    if (error) *error = "existing master-key file is not a regular readable file";
    return false;
  }

  if (kc == KeychainReadResult::kFound) {
    if (!IsHexKey(kc_value)) {
      if (error) *error = "existing Keychain master key has an invalid format";
      return false;
    }
    if (file_state == FileState::kRegular) {
      std::string legacy;
      if (!ReadFileKey(path, &legacy, error)) return false;
      if (legacy != kc_value) {
        if (error) *error = "Keychain and fallback master keys disagree; neither was changed";
        return false;
      }
      std::error_code ignored;
      std::filesystem::remove(path, ignored);
    }
    *key_hex = kc_value;
    return true;
  }

  if (kc == KeychainReadResult::kError) {
    // A pre-migration file is a usable fallback only while it remains readable.
    // If there is no such file, do not invent a different key behind an
    // inaccessible Keychain item.
    if (file_state == FileState::kRegular)
      return ReadFileKey(path, key_hex, error);
    if (error) *error = "Keychain is unavailable and no persisted fallback key exists";
    return false;
  }

  if (file_state == FileState::kRegular) {
    std::string legacy;
    if (!ReadFileKey(path, &legacy, error)) return false;
    std::string observed;
    if (StoreOrReadKeychain(legacy, &observed)) {
      if (observed != legacy) {
        if (error) *error = "a different Keychain key already exists; the fallback was preserved";
        return false;
      }
      std::error_code ignored;
      std::filesystem::remove(path, ignored);
    }
    *key_hex = legacy;
    return true;
  }

  std::string raw_key;
  if (!NewRandomKey(&raw_key, error)) return false;
  const std::string proposed = ToHex(raw_key);
  std::fill(raw_key.begin(), raw_key.end(), '\0');
  std::string observed;
  if (StoreOrReadKeychain(proposed, &observed)) {
    *key_hex = observed;
    return true;
  }
  if (StoreFileKeyExclusive(path, proposed, error)) {
    *key_hex = proposed;
    return true;
  }
  // Another first-run process may have created the exclusive fallback file.
  if (InspectFile(path) == FileState::kRegular &&
      ReadFileKey(path, key_hex, nullptr)) return true;
  if (error && error->empty())
    *error = "neither Keychain nor a new fallback file accepted the master key";
  return false;
}
#endif

bool LoadOrCreateKey(std::string* key_hex, std::string* error) {
  std::filesystem::path dir;
  if (!EnsureUserDataDir(&dir, error)) return false;
  const std::filesystem::path path = dir / "secret.key";
#if defined(__APPLE__)
  return LoadOrCreateMacKey(path, key_hex, error);
#else
  const FileState state = InspectFile(path);
  if (state == FileState::kError) {
    if (error) *error = "existing master-key file is not a regular readable file";
    return false;
  }
  if (state == FileState::kRegular) return ReadFileKey(path, key_hex, error);

  std::string raw_key;
  if (!NewRandomKey(&raw_key, error)) return false;
  const std::string proposed = ToHex(raw_key);
  std::fill(raw_key.begin(), raw_key.end(), '\0');
  if (StoreFileKeyExclusive(path, proposed, error)) {
    *key_hex = proposed;
    return true;
  }
  // Another process may have persisted its key first. Read that key; never
  // truncate or replace the file that won the race.
  if (InspectFile(path) == FileState::kRegular &&
      ReadFileKey(path, key_hex, nullptr)) return true;
  return false;
#endif
}

void InitializeMasterKey() {
  g_key_available = LoadOrCreateKey(&g_key_hex, &g_key_error);
  if (!g_key_available) g_key_hex.clear();
}

}  // namespace

bool SecretKeyHex(std::string* out, std::string* error) {
  if (!out) return false;
  std::call_once(g_key_once, InitializeMasterKey);
  if (!g_key_available) {
    out->clear();
    if (error) *error = g_key_error;
    return false;
  }
  *out = g_key_hex;
  if (error) error->clear();
  return true;
}

}  // namespace platform
}  // namespace shelter
