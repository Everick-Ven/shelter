// SHELTER — safely persisted native master key for secret_crypto.cc.
// Windows: DPAPI-protected secret.key. macOS: login Keychain plus an exclusive
// 0600 file mirror so ad-hoc re-signing cannot strand the key. Existing unreadable
// key material is never replaced.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

#include "src/path_utils.h"
#include "src/platform.h"
#include "src/security_paths.h"
#include "src/secret_crypto.h"
#include "src/secret_key_file.h"

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#endif

namespace shelter {
namespace platform {
namespace {

constexpr size_t kKeyLen = 32;
constexpr std::uintmax_t kMaxPersistedKeyBytes = 64u * 1024u;
constexpr char kKcService[] = "SHELTER";
constexpr char kKcAccount[] = "master-key";

enum class FileState { kMissing, kRegular, kError };

void SecureClear(std::string* value) {
  if (!value) return;
  volatile unsigned char* bytes =
      reinterpret_cast<volatile unsigned char*>(value->data());
  for (size_t i = 0; i < value->size(); ++i) bytes[i] = 0;
  value->clear();
}

std::mutex g_key_mutex;
bool g_key_attempted = false;
bool g_key_available = false;
bool g_key_creation_blocked = false;
bool g_key_attempt_time_set = false;
std::chrono::steady_clock::time_point g_key_last_attempt;
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

size_t Utf8CodePointCount(const std::string& value) {
  size_t count = 0;
  for (size_t i = 0; i < value.size();) {
    const unsigned char lead = static_cast<unsigned char>(value[i]);
    size_t width = 0;
    if (lead <= 0x7f) width = 1;
    else if (lead >= 0xc2 && lead <= 0xdf) width = 2;
    else if (lead >= 0xe0 && lead <= 0xef) width = 3;
    else if (lead >= 0xf0 && lead <= 0xf4) width = 4;
    else return 0;
    if (i + width > value.size()) return 0;
    for (size_t j = 1; j < width; ++j) {
      const unsigned char continuation =
          static_cast<unsigned char>(value[i + j]);
      if ((continuation & 0xc0) != 0x80) return 0;
      if (j == 1 && ((lead == 0xe0 && continuation < 0xa0) ||
                     (lead == 0xed && continuation >= 0xa0) ||
                     (lead == 0xf0 && continuation < 0x90) ||
                     (lead == 0xf4 && continuation > 0x8f))) return 0;
    }
    i += width;
    ++count;
  }
  return count;
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
    std::fill(out->begin(), out->end(), '\0');
    out->clear();
    return false;
  }
  if (file.bad()) {
    std::fill(out->begin(), out->end(), '\0');
    out->clear();
    return false;
  }
  return true;
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
  *dir = path_utils::PathFromUtf8(UserDataDir());
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
  if (error) error->clear();
  if (!IsHexKey(key_hex)) {
    if (error) *error = "generated master key is malformed";
    return false;
  }
  std::string key(kKeyLen, '\0');
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
  std::string write_error;
  const bool written = protected_ok &&
      secret_key_storage::WriteAllExclusive(path, blob, &write_error);
  if (!written) {
    std::fill(blob.begin(), blob.end(), '\0');
    if (error) {
      *error = protected_ok
          ? (write_error.empty() ? "new master key could not be persisted"
                                 : write_error)
          : "new master key could not be protected";
    }
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

bool WaitForMacKeyMirror(const std::filesystem::path& path,
                         std::string* key_hex, std::string* error);

bool EnsureMacKeyMirror(const std::filesystem::path& path,
                        const std::string& key_hex, std::string* error) {
  const FileState state = InspectFile(path);
  if (state == FileState::kError) {
    if (error) *error = "existing master-key file is not a regular readable file";
    return false;
  }
  if (state == FileState::kRegular) {
    std::string mirror;
    if (!WaitForMacKeyMirror(path, &mirror, error)) return false;
    const bool matches = mirror == key_hex;
    std::fill(mirror.begin(), mirror.end(), '\0');
    if (!matches) {
      if (error) *error = "Keychain and fallback master keys disagree; neither was changed";
      return false;
    }
    return true;
  }

  std::string write_error;
  if (StoreFileKeyExclusive(path, key_hex, &write_error)) return true;

  // Другой процесс первого запуска мог создать зеркало одновременно.
  std::string mirror;
  std::string wait_error;
  if (!WaitForMacKeyMirror(path, &mirror, &wait_error)) {
    if (error) {
      *error = write_error.empty()
          ? wait_error
          : write_error + "; " + wait_error;
    }
    return false;
  }
  const bool matches = mirror == key_hex;
  std::fill(mirror.begin(), mirror.end(), '\0');
  if (!matches && error)
    *error = "Keychain and fallback master keys disagree; neither was changed";
  return matches;
}

bool WaitForMacKeyMirror(const std::filesystem::path& path,
                         std::string* key_hex, std::string* error) {
  // Another first-run process may have read Keychain as empty and be writing
  // the file mirror while this process sees a transient Keychain error.
  for (int attempt = 0; attempt < 20; ++attempt) {
    const FileState state = InspectFile(path);
    if (state == FileState::kRegular && ReadFileKey(path, key_hex, nullptr))
      return true;
    if (state == FileState::kError) {
      if (error) *error = "existing master-key file is not a regular readable file";
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (error) *error = "no valid 0600 master-key mirror became available";
  return false;
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
    if (!EnsureMacKeyMirror(path, kc_value, error)) return false;
    *key_hex = kc_value;
    return true;
  }

  if (kc == KeychainReadResult::kError) {
    // An inaccessible Keychain item is recoverable only from the matching
    // owner-only mirror. Never invent a replacement key behind it. The short
    // wait handles another process creating that mirror during first launch.
    return WaitForMacKeyMirror(path, key_hex, error);
  }

  if (file_state == FileState::kRegular) {
    std::string legacy;
    if (!ReadFileKey(path, &legacy, error)) return false;
    std::string observed;
    if (StoreOrReadKeychain(legacy, &observed) && observed != legacy) {
      if (error) *error = "a different Keychain key already exists; the fallback was preserved";
      return false;
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
    if (!EnsureMacKeyMirror(path, observed, error)) return false;
    *key_hex = observed;
    return true;
  }
  std::string write_error;
  if (StoreFileKeyExclusive(path, proposed, &write_error)) {
    *key_hex = proposed;
    return true;
  }
  // Другой процесс первого запуска мог создать fallback-файл одновременно.
  std::string wait_error;
  if (WaitForMacKeyMirror(path, key_hex, &wait_error)) return true;
  if (error) {
    *error = "neither Keychain nor a new fallback file accepted the master key";
    if (!write_error.empty()) *error += "; " + write_error;
    if (!wait_error.empty()) *error += "; " + wait_error;
  }
  return false;
}

// Reads only key material that already exists. The decrypt path must never
// generate a replacement key when a stored ss1 record cannot be opened.
bool LoadExistingMacKey(const std::filesystem::path& path,
                        std::string* key_hex, std::string* error) {
  if (!key_hex) return false;
  const FileState file_state = InspectFile(path);
  if (file_state == FileState::kError) {
    if (error) *error = "existing master-key file is not a regular readable file";
    return false;
  }
  std::string kc_value;
  const KeychainReadResult kc = ReadKeychain(kKcService, kKcAccount, &kc_value);
  if (kc == KeychainReadResult::kFound) {
    if (!IsHexKey(kc_value)) {
      if (error) *error = "existing Keychain master key has an invalid format";
      std::fill(kc_value.begin(), kc_value.end(), '\0');
      return false;
    }
    if (file_state == FileState::kRegular) {
      std::string mirror;
      if (!ReadFileKey(path, &mirror, error)) {
        std::fill(kc_value.begin(), kc_value.end(), '\0');
        return false;
      }
      const bool matches = mirror == kc_value;
      std::fill(mirror.begin(), mirror.end(), '\0');
      if (!matches) {
        if (error) *error = "Keychain and fallback master keys disagree; neither was changed";
        std::fill(kc_value.begin(), kc_value.end(), '\0');
        return false;
      }
    }
    *key_hex = kc_value;
    std::fill(kc_value.begin(), kc_value.end(), '\0');
    return true;
  }
  std::fill(kc_value.begin(), kc_value.end(), '\0');
  if (file_state == FileState::kRegular) {
    return ReadFileKey(path, key_hex, error);
  }
  if (error) {
    *error = kc == KeychainReadResult::kError
        ? "login Keychain is unavailable and no master-key mirror could be read"
        : "no existing master key was found; refusing to create one for decryption";
  }
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

bool LoadExistingKey(std::string* key_hex, std::string* error) {
  if (!key_hex) return false;
  std::filesystem::path dir;
  if (!EnsureUserDataDir(&dir, error)) return false;
  const std::filesystem::path path = dir / "secret.key";
#if defined(__APPLE__)
  return LoadExistingMacKey(path, key_hex, error);
#else
  const FileState state = InspectFile(path);
  if (state == FileState::kError) {
    if (error) *error = "existing master-key file is not a regular readable file";
    return false;
  }
  if (state == FileState::kMissing) {
    if (error) *error = "existing master-key file is missing; refusing to create one for decryption";
    return false;
  }
  return ReadFileKey(path, key_hex, error);
#endif
}

constexpr char kPasswordWrapFile[] = "secret.key.password";
constexpr std::uintmax_t kMaxPasswordWrapBytes = 1024;

std::filesystem::path PasswordWrapPath() {
  const std::filesystem::path dir = path_utils::PathFromUtf8(UserDataDir());
  return dir.empty() ? std::filesystem::path() : dir / kPasswordWrapFile;
}

bool LoadKeyLocked(bool may_create, std::string* error) {
  if (g_key_available) {
    if (error) error->clear();
    return true;
  }
  const auto now = std::chrono::steady_clock::now();
  if (g_key_attempt_time_set && now - g_key_last_attempt <
          std::chrono::milliseconds(500)) {
    if (error) *error = g_key_error;
    return false;
  }
  g_key_last_attempt = now;
  g_key_attempt_time_set = true;

  std::string candidate, load_error;
  const bool create = may_create && !g_key_attempted && !g_key_creation_blocked;
  const bool ok = create ? LoadOrCreateKey(&candidate, &load_error)
                         : LoadExistingKey(&candidate, &load_error);
  g_key_attempted = true;
  if (ok && IsHexKey(candidate)) {
    SecureClear(&g_key_hex);
    g_key_hex = candidate;
    g_key_available = true;
    g_key_creation_blocked = false;
    g_key_error.clear();
    SecureClear(&candidate);
    if (error) error->clear();
    LogSecretKeyDiagnostic("master key is available; no stored key was replaced");
    return true;
  }
  SecureClear(&candidate);
  g_key_available = false;
  if (load_error.empty()) load_error = "master key is unavailable";
  g_key_error = load_error;
  LogSecretKeyDiagnostic("master key load failed: " + load_error);
  if (error) *error = g_key_error;
  return false;
}

std::string NewTemporarySuffix() {
  std::string bytes;
  if (!RandomBytes(8, &bytes) || bytes.size() != 8) return std::string();
  const std::string suffix = ToHex(bytes);
  std::fill(bytes.begin(), bytes.end(), '\0');
  return suffix;
}

}  // namespace

bool SecretKeyHex(std::string* out, std::string* error) {
  if (!out) return false;
  std::lock_guard<std::mutex> lock(g_key_mutex);
  if (!LoadKeyLocked(true, error)) {
    out->clear();
    return false;
  }
  *out = g_key_hex;
  if (error) error->clear();
  return true;
}

bool SecretKeyHexForDecrypt(std::string* out, std::string* error) {
  if (!out) return false;
  std::lock_guard<std::mutex> lock(g_key_mutex);
  if (!LoadKeyLocked(false, error)) {
    g_key_creation_blocked = true;
    out->clear();
    return false;
  }
  *out = g_key_hex;
  if (error) error->clear();
  return true;
}

bool HasSecretKeyPassword() {
  const FileState state = InspectFile(PasswordWrapPath());
  return state == FileState::kRegular;
}

bool SetSecretKeyPassword(const std::string& password, std::string* error) {
  if (error) error->clear();
  if (Utf8CodePointCount(password) < 10 || password.size() > 1024) {
    if (error) *error = "master password must contain at least 10 UTF-8 characters";
    return false;
  }
  std::string master_key;
  {
    std::lock_guard<std::mutex> lock(g_key_mutex);
    // On a clean first run there may not be a key yet; create the original
    // random key before wrapping it. A prior decrypt failure sets
    // g_key_creation_blocked, so this still cannot replace a missing key.
    if (!LoadKeyLocked(true, error)) return false;
    master_key = g_key_hex;
  }

  std::string salt, nonce;
  if (!RandomBytes(16, &salt) || salt.size() != 16 ||
      !RandomBytes(12, &nonce) || nonce.size() != 12) {
    SecureClear(&master_key);
    std::fill(salt.begin(), salt.end(), '\0');
    std::fill(nonce.begin(), nonce.end(), '\0');
    if (error) *error = "operating system random generator failed";
    return false;
  }
  std::string wrapped;
  const bool wrapped_ok = secret_crypto::WrapMasterKeyWithPassword(
      master_key, password, salt, nonce,
      secret_crypto::kPasswordWrapDefaultIterations, &wrapped);
  SecureClear(&master_key);
  std::fill(salt.begin(), salt.end(), '\0');
  std::fill(nonce.begin(), nonce.end(), '\0');
  if (!wrapped_ok) {
    if (error) *error = "master-key password wrapper could not be created";
    return false;
  }

  std::filesystem::path dir;
  if (!EnsureUserDataDir(&dir, error)) {
    std::fill(wrapped.begin(), wrapped.end(), '\0');
    return false;
  }
  const std::filesystem::path path = dir / kPasswordWrapFile;
  const FileState state = InspectFile(path);
  if (state == FileState::kError) {
    if (error) *error = "existing master-key password file is not a regular file";
    return false;
  }
  bool written = false;
  std::string write_error;
  if (state == FileState::kMissing) {
    written = secret_key_storage::WriteAllExclusive(path, wrapped, &write_error);
  } else {
    const std::string suffix = NewTemporarySuffix();
    if (suffix.empty()) {
      write_error = "operating system random generator failed";
    } else {
      std::filesystem::path temporary = path;
      temporary += ".tmp-" + suffix;
      written = secret_key_storage::ReplaceFileAtomically(
          path, temporary, wrapped, &write_error);
    }
  }
  if (!written) {
    std::fill(wrapped.begin(), wrapped.end(), '\0');
    if (error) *error = write_error.empty()
        ? "master-key password wrapper could not be saved"
        : write_error;
    return false;
  }
  std::string read_back;
  const bool verified = ReadAll(path, &read_back) && read_back == wrapped;
  std::fill(read_back.begin(), read_back.end(), '\0');
  std::fill(wrapped.begin(), wrapped.end(), '\0');
  if (!verified) {
    if (error) *error = "master-key password wrapper failed read-back verification";
    return false;
  }
  LogSecretKeyDiagnostic("master-key password recovery wrapper saved and verified");
  if (error) error->clear();
  return true;
}

bool UnlockSecretKeyWithPassword(const std::string& password,
                                 std::string* error) {
  if (error) error->clear();
  const std::filesystem::path path = PasswordWrapPath();
  std::string wrapped;
  if (path.empty() || InspectFile(path) != FileState::kRegular ||
      !ReadAll(path, &wrapped) ||
      wrapped.size() > kMaxPasswordWrapBytes) {
    if (error) *error = "master-key password wrapper is unavailable";
    return false;
  }
  std::string recovered;
  const bool ok = secret_crypto::UnwrapMasterKeyWithPassword(
      password, wrapped, &recovered);
  std::fill(wrapped.begin(), wrapped.end(), '\0');
  if (!ok || !IsHexKey(recovered)) {
    SecureClear(&recovered);
    LogSecretKeyDiagnostic("master-key password unlock failed");
    if (error) *error = "master-key password was rejected or the wrapper is damaged";
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_key_mutex);
    SecureClear(&g_key_hex);
    g_key_hex = recovered;
    g_key_available = true;
    g_key_attempted = true;
    g_key_creation_blocked = false;
    g_key_error.clear();
    g_key_attempt_time_set = true;
    g_key_last_attempt = std::chrono::steady_clock::now();
  }
  SecureClear(&recovered);
  LogSecretKeyDiagnostic("master key unlocked with password wrapper");
  return true;
}

bool RemoveSecretKeyPassword(std::string* error) {
  if (error) error->clear();
  std::string cached_key;
  {
    std::lock_guard<std::mutex> lock(g_key_mutex);
    if (!LoadKeyLocked(false, error)) return false;
    cached_key = g_key_hex;
  }
  std::string stored_key, stored_error;
  const bool matching_persistent_key =
      LoadExistingKey(&stored_key, &stored_error) && stored_key == cached_key;
  SecureClear(&cached_key);
  SecureClear(&stored_key);
  if (!matching_persistent_key) {
    if (error) *error = "system master-key storage is unavailable or differs; password wrapper was retained";
    return false;
  }
  const std::filesystem::path path = PasswordWrapPath();
  const FileState state = InspectFile(path);
  if (state == FileState::kMissing) return true;
  if (state != FileState::kRegular) {
    if (error) *error = "master-key password file is not a regular file";
    return false;
  }
  std::error_code ec;
  if (!std::filesystem::remove(path, ec) || ec) {
    if (error) *error = "master-key password wrapper could not be removed";
    return false;
  }
  LogSecretKeyDiagnostic("master-key password recovery wrapper removed");
  return true;
}

}  // namespace platform
}  // namespace shelter
