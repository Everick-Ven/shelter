// SHELTER — системные примитивы для проверки CRX3.
#ifndef SHELTER_CRX3_CRYPTO_H_
#define SHELTER_CRX3_CRYPTO_H_

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace shelter {
namespace crx3 {
namespace crypto {

bool Sha256(const std::string& bytes, std::string* digest);
bool Sha256Chunks(
    const std::vector<std::pair<const char*, std::size_t>>& chunks,
    std::string* digest);
bool VerifySignatureDigest(const std::string& public_key_der, bool ecdsa,
                           const std::string& digest,
                           const std::string& signature);

}  // namespace crypto
}  // namespace crx3
}  // namespace shelter

#endif  // SHELTER_CRX3_CRYPTO_H_
