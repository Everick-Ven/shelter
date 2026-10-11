#include "src/secret_crypto.h"
#include "src/platform.h"

#include <cassert>
#include <string>

namespace shelter {
namespace platform {

// The test covers only the deterministic crypto core; production entry points
// are deliberately unavailable in this process.
bool RandomBytes(size_t, std::string*) { return false; }
bool SecretKeyHex(std::string*, std::string*) { return false; }

}  // namespace platform
}  // namespace shelter

int main() {
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
  return 0;
}
