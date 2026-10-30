#include "src/secret_crypto.h"
#include "src/platform.h"
#include "src/secret_key_file.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace {

std::string Hex(const std::string& bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (unsigned char byte : bytes) {
    out.push_back(kHex[byte >> 4]);
    out.push_back(kHex[byte & 0x0f]);
  }
  return out;
}

void CheckSecretKeyFileStorage() {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path temp = fs::temp_directory_path(ec) /
      ("shelter-master-key-file-test-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  assert(!ec);
  assert(fs::create_directory(temp, ec) && !ec);

  const fs::path key_file = temp / "secret.key";
  std::string key(32, '\0');
  for (size_t i = 0; i < key.size(); ++i)
    key[i] = static_cast<char>(i);
  std::string error;
  assert(shelter::platform::secret_key_storage::WriteAllExclusive(
      key_file, key, &error));
  assert(error.empty());

  std::ifstream file(key_file, std::ios::binary);
  assert(file);
  const std::string stored((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  assert(stored == key);
  file.close();

#if !defined(_WIN32)
  const fs::perms mode = fs::status(key_file, ec).permissions();
  const fs::perms access = fs::perms::owner_all | fs::perms::group_all |
                           fs::perms::others_all;
  const fs::perms expected = fs::perms::owner_read | fs::perms::owner_write;
  assert(!ec);
  assert((mode & access) == expected);
#endif

  error.clear();
  assert(!shelter::platform::secret_key_storage::WriteAllExclusive(
      key_file, "replacement", &error));
  assert(!error.empty());
  std::ifstream preserved_file(key_file, std::ios::binary);
  const std::string preserved((std::istreambuf_iterator<char>(preserved_file)),
                              std::istreambuf_iterator<char>());
  assert(preserved == key);
  preserved_file.close();

  const fs::path replace_temp = temp / "secret.key.new";
  error.clear();
  assert(shelter::platform::secret_key_storage::ReplaceFileAtomically(
      key_file, replace_temp, "replacement", &error));
  assert(error.empty());
  std::ifstream replaced_file(key_file, std::ios::binary);
  const std::string replaced((std::istreambuf_iterator<char>(replaced_file)),
                             std::istreambuf_iterator<char>());
  assert(replaced == "replacement");
  replaced_file.close();
  assert(!fs::exists(replace_temp));

  const fs::path linked_file = temp / "linked.key";
  ec.clear();
  fs::create_symlink(key_file, linked_file, ec);
  if (!ec) {
    error.clear();
    assert(!shelter::platform::secret_key_storage::WriteAllExclusive(
        linked_file, "replacement", &error));
    assert(!error.empty());
    std::ifstream target_file(key_file, std::ios::binary);
    const std::string target((std::istreambuf_iterator<char>(target_file)),
                             std::istreambuf_iterator<char>());
    assert(target == "replacement");
    target_file.close();
  }

  ec.clear();
  fs::remove_all(temp, ec);
  assert(!ec);
}

}  // namespace

namespace shelter {
namespace platform {

// The test covers only the deterministic crypto core; production entry points
// are deliberately unavailable in this process.
bool RandomBytes(size_t, std::string*) { return false; }
bool SecretKeyHex(std::string*, std::string*) { return false; }
bool SecretKeyHexForDecrypt(std::string*, std::string*) { return false; }
void LogSecretKeyDiagnostic(const std::string&) {}

}  // namespace platform
}  // namespace shelter

int main() {
  CheckSecretKeyFileStorage();

  const std::string key =
      "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
  const std::string plaintext = "compatibility test: Shelter ss1";
  // Fixed legacy host-bridge.js Sec.enc vector; verifies byte-for-byte ss1
  // compatibility before native crypto replaces the renderer implementation.
  const char nonce_bytes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  const std::string nonce(nonce_bytes, sizeof(nonce_bytes));
  const std::string expected =
      "ss1:AAECAwQFBgcICQoLyvM44h3YnGs4Oe5WPVQC5lk2Yivtu//5xy9XHJu3wYNA6mvu3T2p8YWWv4dVqLsr2XTuK3scGlviVuGkr+r3";

  std::string ciphertext;
  assert(shelter::secret_crypto::EncryptWithKey(key, plaintext, nonce,
                                                 &ciphertext));
  assert(ciphertext == expected);

  std::string recovered;
  assert(shelter::secret_crypto::DecryptWithKey(key, expected, &recovered));
  assert(recovered == plaintext);

  const std::string wrong_key =
      "100102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
  assert(!shelter::secret_crypto::DecryptWithKey(wrong_key, expected,
                                                 &recovered));
  assert(recovered.empty());

  std::string tampered = expected;
  tampered.back() = tampered.back() == 'A' ? 'B' : 'A';
  assert(!shelter::secret_crypto::DecryptWithKey(key, tampered, &recovered));
  assert(recovered.empty());

  assert(!shelter::secret_crypto::EncryptWithKey(key, plaintext, "short",
                                                  &ciphertext));


  // Канонические векторы SHA-256 (FIPS 180-4): Sha256Hex используется для
  // отпечатков сертификатов при пининге исключений (L5).
  assert(shelter::secret_crypto::Sha256Hex("") ==
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  assert(shelter::secret_crypto::Sha256Hex("abc") ==
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  // RFC 7914-style PBKDF2-HMAC-SHA256 vectors; a single iteration makes the
  // KDF composition test fast while still checking its byte-level semantics.
  std::string derived;
  assert(shelter::secret_crypto::Pbkdf2HmacSha256(
      "password", "salt", 1, 32, &derived));
  assert(Hex(derived) ==
         "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
  assert(shelter::secret_crypto::Pbkdf2HmacSha256(
      "password", "salt", 2, 32, &derived));
  assert(Hex(derived) ==
         "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
  assert(!shelter::secret_crypto::Pbkdf2HmacSha256(
      "password", "salt", 0, 32, &derived));
  assert(derived.empty());

  const std::string recovery_password = "correct horse battery staple";
  const std::string recovery_salt(16, 's');
  const std::string recovery_nonce(12, 'n');
  std::string wrapped_key, unwrapped_key;
  assert(shelter::secret_crypto::WrapMasterKeyWithPassword(
      key, recovery_password, recovery_salt, recovery_nonce, 2, &wrapped_key));
  assert(wrapped_key.rfind("skw1:2:", 0) == 0);
  assert(!shelter::secret_crypto::UnwrapMasterKeyWithPassword(
      recovery_password, wrapped_key, &unwrapped_key));
  assert(unwrapped_key.empty());
  assert(shelter::secret_crypto::UnwrapMasterKeyWithPassword(
      recovery_password, wrapped_key, &unwrapped_key, 1));
  assert(unwrapped_key == key);
  assert(!shelter::secret_crypto::UnwrapMasterKeyWithPassword(
      "a different master password", wrapped_key, &unwrapped_key, 1));
  assert(unwrapped_key.empty());
  assert(!shelter::secret_crypto::UnwrapMasterKeyWithPassword(
      recovery_password, "skw1:broken", &unwrapped_key, 1));
  assert(unwrapped_key.empty());
  assert(!shelter::secret_crypto::UnwrapMasterKeyWithPassword(
      recovery_password, "skw1:2000001:00000000000000000000000000000000:bad",
      &unwrapped_key, 1));
  assert(unwrapped_key.empty());
  return 0;
}
