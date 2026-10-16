// SHELTER — проверка подписи CRX3 системной криптографией.
#include "src/crx3.h"

#include "src/crx3_format.h"
#include "src/platform.h"

namespace shelter {
namespace crx3 {
namespace {

bool ExtensionIdFromKey(const std::string& public_key, std::string* id,
                        std::string* id_bytes) {
  if (!id || !id_bytes) return false;
  std::string digest;
  if (!platform::Sha256(public_key, &digest) || digest.size() != 32)
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

}  // namespace

bool Verify(const std::string& bytes, const std::string& expected_id,
            std::size_t* zip_offset, std::string* extension_id) {
  if (!zip_offset || !extension_id) return false;
  Package package;
  if (!Parse(bytes, &package) || package.zip_offset >= bytes.size())
    return false;

  std::string derived_id;
  bool has_valid_signature = false;
  for (const Proof& proof : package.proofs) {
    std::string proof_id, proof_id_bytes;
    if (!ExtensionIdFromKey(proof.public_key, &proof_id, &proof_id_bytes) ||
        proof_id_bytes != package.crx_id ||
        (!expected_id.empty() && proof_id != expected_id)) {
      // CRX3 may carry more than one proof. Only a proof whose key identifies
      // this package (and the requested store ID, when present) is relevant.
      continue;
    }
    if (platform::VerifyCrx3Signature(
            proof.public_key, proof.ecdsa, package.signed_header_data,
            bytes.data() + package.zip_offset,
            bytes.size() - package.zip_offset, proof.signature)) {
      has_valid_signature = true;
      derived_id = proof_id;
      break;
    }
  }
  if (!has_valid_signature || derived_id.empty()) return false;
  *zip_offset = package.zip_offset;
  *extension_id = derived_id;
  return true;
}

}  // namespace crx3
}  // namespace shelter
