// Standalone integration coverage for the native master-key recovery layer.
// Uses a private temporary directory and deterministic test-only random bytes.
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "src/platform.h"
#include "src/secret_crypto.h"

namespace {
std::string g_user_data_dir;
std::uint8_t g_random_byte = 0;

bool ReadFile(const std::filesystem::path& path, std::string* out) {
  std::ifstream file(path, std::ios::binary);
  if (!file || !out) return false;
  *out = std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
  return file.eof() || !file.bad();
}

void WriteFile(const std::filesystem::path& path, const std::string& value) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  assert(file);
  file.write(value.data(), static_cast<std::streamsize>(value.size()));
  assert(file.good());
}

void MissingKeyDoesNotCreateReplacement() {
  const std::filesystem::path key_path =
      std::filesystem::path(g_user_data_dir) / "secret.key";
  std::string key, error;
  assert(!shelter::platform::SecretKeyHexForDecrypt(&key, &error));
  assert(key.empty());
  assert(!std::filesystem::exists(key_path));

  // A later encrypt request in this same process must not turn an earlier
  // decrypt failure into creation of a replacement key.
  assert(!shelter::platform::SecretKeyHex(&key, &error));
  assert(key.empty());
  assert(!std::filesystem::exists(key_path));
  assert(!shelter::platform::SetSecretKeyPassword(
      "a long recovery passphrase", &error));
  assert(!std::filesystem::exists(key_path));
  assert(!shelter::platform::HasSecretKeyPassword());
}

void CreatePasswordWrapper() {
  const std::filesystem::path dir(g_user_data_dir);
  const std::filesystem::path key_path = dir / "secret.key";
  const std::filesystem::path wrapper_path = dir / "secret.key.password";
  const std::filesystem::path sample_path = dir / "sample.ss1";
  const std::filesystem::path expected_path = dir / "expected-key.txt";
  std::string key, error, ciphertext;
  const std::string password = "a long recovery passphrase";

  assert(shelter::platform::SecretKeyHex(&key, &error));
  assert(key.size() == 64);
  assert(!shelter::platform::SetSecretKeyPassword("пароль123", &error));
  assert(!shelter::platform::HasSecretKeyPassword());
  WriteFile(expected_path, key);
  assert(shelter::secret_crypto::Encrypt("preserve the existing ss1 board",
                                         &ciphertext));
  WriteFile(sample_path, ciphertext);

  // Setup wraps the already-persisted key that encrypted this ss1 sample.
  assert(shelter::platform::SetSecretKeyPassword(password, &error));
  assert(shelter::platform::HasSecretKeyPassword());
  std::string wrapper;
  assert(ReadFile(wrapper_path, &wrapper));
  assert(wrapper.rfind("skw1:600000:", 0) == 0);
  assert(wrapper.find(password) == std::string::npos);
  assert(wrapper.find(key) == std::string::npos);
  assert(std::filesystem::exists(key_path));
  assert(shelter::platform::RemoveSecretKeyPassword(&error));
  assert(!shelter::platform::HasSecretKeyPassword());
  assert(shelter::platform::SetSecretKeyPassword(password, &error));
  assert(shelter::platform::HasSecretKeyPassword());
}

void FirstRunSetupCreatesAndWrapsOneKey() {
  const std::filesystem::path dir(g_user_data_dir);
  const std::filesystem::path key_path = dir / "secret.key";
  const std::filesystem::path wrapper_path = dir / "secret.key.password";
  std::string key, error, ciphertext, plaintext;
  const std::string password = "a long recovery passphrase";

  assert(!std::filesystem::exists(key_path));
  assert(!shelter::platform::SetSecretKeyPassword("пароль123", &error));
  assert(!std::filesystem::exists(key_path));
  assert(shelter::platform::SetSecretKeyPassword(password, &error));
  assert(std::filesystem::exists(key_path));
  assert(std::filesystem::exists(wrapper_path));
  assert(shelter::platform::SecretKeyHex(&key, &error));
  assert(key.size() == 64);
  assert(shelter::secret_crypto::Encrypt("new profile ss1 sample", &ciphertext));
  assert(shelter::secret_crypto::Decrypt(ciphertext, &plaintext));
  assert(plaintext == "new profile ss1 sample");
}

void UnlockFromWrapperWithoutSystemKey() {
  const std::filesystem::path dir(g_user_data_dir);
  const std::filesystem::path key_path = dir / "secret.key";
  const std::filesystem::path sample_path = dir / "sample.ss1";
  const std::filesystem::path expected_path = dir / "expected-key.txt";
  std::error_code ec;
  assert(std::filesystem::remove(key_path, ec) && !ec);

  std::string key, error, ciphertext, plaintext, expected_key;
  // Missing OS-protected key must not cause a new key file to be created.
  assert(!shelter::platform::SecretKeyHexForDecrypt(&key, &error));
  assert(!std::filesystem::exists(key_path));

  assert(shelter::platform::UnlockSecretKeyWithPassword(
      "a long recovery passphrase", &error));
  assert(shelter::platform::SecretKeyHexForDecrypt(&key, &error));
  assert(ReadFile(expected_path, &expected_key));
  assert(key == expected_key);
  assert(ReadFile(sample_path, &ciphertext));
  assert(shelter::secret_crypto::Decrypt(ciphertext, &plaintext));
  assert(plaintext == "preserve the existing ss1 board");

  assert(!shelter::platform::RemoveSecretKeyPassword(&error));
  assert(shelter::platform::HasSecretKeyPassword());
  assert(std::filesystem::exists(dir / "secret.key.password"));
}
}  // namespace

namespace shelter {
namespace platform {

std::string UserDataDir() { return g_user_data_dir; }

bool RandomBytes(size_t size, std::string* out) {
  if (!out) return false;
  out->resize(size);
  for (char& byte : *out) byte = static_cast<char>(++g_random_byte);
  return true;
}

void LogSecretKeyDiagnostic(const std::string&) {}

}  // namespace platform
}  // namespace shelter

int main(int argc, char** argv) {
  assert(argc == 3);
  const std::string mode = argv[1];
  g_user_data_dir = argv[2];
  if (mode == "missing") {
    assert(!std::filesystem::exists(g_user_data_dir));
    std::filesystem::create_directories(g_user_data_dir);
    MissingKeyDoesNotCreateReplacement();
  } else if (mode == "setup") {
    assert(!std::filesystem::exists(g_user_data_dir));
    std::filesystem::create_directories(g_user_data_dir);
    CreatePasswordWrapper();
  } else if (mode == "first-run") {
    assert(!std::filesystem::exists(g_user_data_dir));
    std::filesystem::create_directories(g_user_data_dir);
    FirstRunSetupCreatesAndWrapsOneKey();
  } else if (mode == "unlock") {
    UnlockFromWrapperWithoutSystemKey();
  } else {
    assert(false && "unknown test mode");
  }
  return 0;
}
