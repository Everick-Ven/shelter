// SHELTER — проверка подписи CRX3 системной криптографией.
#include "src/crx3.h"

#include "src/crx3_crypto.h"
#include "src/crx3_format.h"

#include <cstdint>
#include <string>
#include <utility>

namespace shelter {
namespace crx3 {
namespace {

bool ExtensionIdFromKey(const std::string& public_key, std::string* id,
                        std::string* id_bytes) {
  if (!id || !id_bytes || public_key.empty() || public_key.size() > 8192)
    return false;
  std::string digest;
  if (!crypto::Sha256(public_key, &digest) || digest.size() != 32)
    return false;
  id_bytes->assign(digest.data(), 16);
  id->clear();
  id->reserve(32);
  for (unsigned char byte : *id_bytes) {
    id->push_back(static_cast<char>('a' + (byte >> 4)));
    id->push_back(static_cast<char>('a' + (byte & 0x0f)));
  }
  return true;
}

bool SignedDataDigest(const std::string& signed_header_data,
                      const char* zip_data, std::size_t zip_size,
                      std::string* digest) {
  if (!digest || signed_header_data.size() > UINT32_MAX || !zip_data ||
      zip_size == 0 || zip_size > 64u * 1024u * 1024u) {
    return false;
  }

  // Формат signed input задан в crx3.proto:
  // https://github.com/chromium/chromium/blob/e15cfb65a092b26224d6bdc685ed2aa9f41f7eb6/components/crx_file/crx3.proto
  // Контекст включает завершающий NUL "CRX3 SignedData\x00".
  constexpr char kContext[] = "CRX3 SignedData";
  static_assert(sizeof(kContext) == 16);
  const std::uint32_t header_size =
      static_cast<std::uint32_t>(signed_header_data.size());
  const char header_size_le[4] = {
      static_cast<char>(header_size), static_cast<char>(header_size >> 8),
      static_cast<char>(header_size >> 16),
      static_cast<char>(header_size >> 24)};
  return crypto::Sha256Chunks(
             {{kContext, sizeof(kContext)},
              {header_size_le, sizeof(header_size_le)},
              {signed_header_data.data(), signed_header_data.size()},
              {zip_data, zip_size}},
             digest) &&
         digest->size() == 32;
}

}  // namespace

bool ExtensionIdFromPublicKeyDer(const std::string& public_key_der,
                                 std::string* extension_id) {
  std::string id_bytes;
  return ExtensionIdFromKey(public_key_der, extension_id, &id_bytes);
}

bool Verify(const std::string& bytes, const std::string& expected_id,
            std::size_t* zip_offset, std::string* extension_id) {
  if (!zip_offset || !extension_id) return false;
  Package package;
  if (!Parse(bytes, &package) || package.zip_offset >= bytes.size() ||
      bytes.size() - package.zip_offset > 64u * 1024u * 1024u) {
    return false;
  }

  const char* const zip_data = bytes.data() + package.zip_offset;
  const std::size_t zip_size = bytes.size() - package.zip_offset;
  std::string digest;
  if (!SignedDataDigest(package.signed_header_data, zip_data, zip_size,
                        &digest)) {
    return false;
  }

  std::string derived_id;
  for (const Proof& proof : package.proofs) {
    std::string proof_id, proof_id_bytes;
    if (!ExtensionIdFromKey(proof.public_key, &proof_id, &proof_id_bytes) ||
        proof_id_bytes != package.crx_id ||
        (!expected_id.empty() && proof_id != expected_id)) {
      // CRX3 может содержать несколько proof. Подходит только ключ,
      // соответствующий подписанному crx_id и ожидаемому ID магазина.
      continue;
    }
    if (crypto::VerifySignatureDigest(proof.public_key, proof.ecdsa, digest,
                                      proof.signature)) {
      derived_id = proof_id;
      break;
    }
  }
  if (derived_id.empty()) return false;
  *zip_offset = package.zip_offset;
  *extension_id = derived_id;
  return true;
}

}  // namespace crx3
}  // namespace shelter
