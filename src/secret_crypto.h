// SHELTER — native ss1 secret encryption (ChaCha20 + HMAC-SHA256).
#ifndef SHELTER_SECRET_CRYPTO_H_
#define SHELTER_SECRET_CRYPTO_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace shelter {
namespace secret_crypto {

// Production entry points. The master key remains in the browser process.
bool Encrypt(const std::string& plaintext, std::string* ciphertext);
bool Decrypt(const std::string& ciphertext, std::string* plaintext);

// SHA-256 в hex для нешифровальных нужд (отпечатки сертификатов для пининга).
// Пустая строка на входе даёт корректный дайджест пустой строки.
std::string Sha256Hex(const std::string& input);

// Standard PBKDF2-HMAC-SHA256. Password and salt are byte strings (the UI
// passes UTF-8); failures clear key_out. The native recovery wrapper pins its
// cost and version separately instead of trusting parameters from the file.
bool Pbkdf2HmacSha256(const std::string& password, const std::string& salt,
                      std::uint32_t iterations, std::size_t output_bytes,
                      std::string* key_out);

// Protect/unprotect an existing random 256-bit master key. The versioned skw1
// record carries only a KDF cost, random salt and an ss1-wrapped key; it never
// stores the password or plaintext master key. The record carries a bounded
// PBKDF2 cost (production default 600000), a 16-byte salt and an ss1-wrapped
// key; readers enforce at least the production cost. The nonce is 12 bytes.
inline constexpr std::uint32_t kPasswordWrapDefaultIterations = 600000;
bool WrapMasterKeyWithPassword(const std::string& master_key_hex,
                               const std::string& password,
                               const std::string& salt,
                               const std::string& nonce,
                               std::uint32_t iterations,
                               std::string* wrapped);
bool UnwrapMasterKeyWithPassword(
    const std::string& password, const std::string& wrapped,
    std::string* master_key_hex,
    std::uint32_t minimum_iterations = kPasswordWrapDefaultIterations);

// Deterministic core used by the standalone compatibility tests. Production
// callers use Encrypt(), which always obtains a fresh OS-CSPRNG nonce.
bool EncryptWithKey(const std::string& key_hex, const std::string& plaintext,
                    const std::string& nonce, std::string* ciphertext);
bool DecryptWithKey(const std::string& key_hex, const std::string& ciphertext,
                    std::string* plaintext);

}  // namespace secret_crypto
}  // namespace shelter

#endif  // SHELTER_SECRET_CRYPTO_H_
