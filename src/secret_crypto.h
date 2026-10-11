// SHELTER — native ss1 secret encryption (ChaCha20 + HMAC-SHA256).
#ifndef SHELTER_SECRET_CRYPTO_H_
#define SHELTER_SECRET_CRYPTO_H_

#include <string>

namespace shelter {
namespace secret_crypto {

// Production entry points. The master key remains in the browser process.
bool Encrypt(const std::string& plaintext, std::string* ciphertext);
bool Decrypt(const std::string& ciphertext, std::string* plaintext);

// Deterministic core used by the standalone compatibility tests. Production
// callers use Encrypt(), which always obtains a fresh OS-CSPRNG nonce.
bool EncryptWithKey(const std::string& key_hex, const std::string& plaintext,
                    const std::string& nonce, std::string* ciphertext);
bool DecryptWithKey(const std::string& key_hex, const std::string& ciphertext,
                    std::string* plaintext);

}  // namespace secret_crypto
}  // namespace shelter

#endif  // SHELTER_SECRET_CRYPTO_H_
