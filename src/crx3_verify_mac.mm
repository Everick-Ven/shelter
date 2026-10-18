// SHELTER — проверка подписей CRX3 через Security.framework/CommonCrypto.
#import <Security/Security.h>

#include <CommonCrypto/CommonDigest.h>

#include "src/crx3_crypto.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace shelter {
namespace crx3 {
namespace crypto {

bool Sha256Chunks(
    const std::vector<std::pair<const char*, std::size_t>>& chunks,
    std::string* digest) {
  if (!digest) return false;
  digest->clear();
  CC_SHA256_CTX context{};
  if (CC_SHA256_Init(&context) == 0) return false;
  for (const auto& chunk : chunks) {
    if (chunk.second && !chunk.first) return false;
    std::size_t offset = 0;
    while (offset < chunk.second) {
      const CC_LONG count = static_cast<CC_LONG>(std::min<std::size_t>(
          chunk.second - offset, static_cast<std::size_t>(UINT32_MAX)));
      if (CC_SHA256_Update(&context, chunk.first + offset, count) == 0) {
        std::memset(&context, 0, sizeof(context));
        return false;
      }
      offset += count;
    }
  }
  digest->assign(CC_SHA256_DIGEST_LENGTH, '\0');
  if (CC_SHA256_Final(
          reinterpret_cast<unsigned char*>(digest->data()), &context) == 0) {
    std::memset(&context, 0, sizeof(context));
    digest->clear();
    return false;
  }
  std::memset(&context, 0, sizeof(context));
  return true;
}

bool Sha256(const std::string& bytes, std::string* digest) {
  return Sha256Chunks({{bytes.data(), bytes.size()}}, digest);
}

namespace {

struct DerItem {
  std::uint8_t tag = 0;
  const std::uint8_t* data = nullptr;
  std::size_t size = 0;
};

bool ReadDerItem(const std::uint8_t* bytes, std::size_t size,
                 std::size_t* offset, DerItem* item) {
  if (!bytes || !offset || !item || *offset >= size) return false;
  const std::uint8_t tag = bytes[(*offset)++];
  if (*offset >= size) return false;
  const std::uint8_t first = bytes[(*offset)++];
  std::size_t length = 0;
  if ((first & 0x80) == 0) {
    length = first;
  } else {
    const std::size_t count = first & 0x7f;
    if (count == 0 || count > sizeof(std::size_t) ||
        count > size - *offset || bytes[*offset] == 0) {
      return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
      if (length > (std::numeric_limits<std::size_t>::max() >> 8))
        return false;
      length = (length << 8) | bytes[(*offset)++];
    }
    if (length < 128) return false;
  }
  if (length > size - *offset) return false;
  item->tag = tag;
  item->data = bytes + *offset;
  item->size = length;
  *offset += length;
  return true;
}

bool OidEquals(const DerItem& item, const std::uint8_t* oid,
               std::size_t oid_size) {
  return item.tag == 0x06 && item.size == oid_size &&
         std::memcmp(item.data, oid, oid_size) == 0;
}

bool RsaBits(const std::uint8_t* bytes, std::size_t size, int* bits) {
  if (!bytes || !bits) return false;
  std::size_t offset = 0;
  DerItem sequence;
  if (!ReadDerItem(bytes, size, &offset, &sequence) || sequence.tag != 0x30 ||
      offset != size) {
    return false;
  }
  std::size_t child = 0;
  DerItem modulus, exponent;
  if (!ReadDerItem(sequence.data, sequence.size, &child, &modulus) ||
      modulus.tag != 0x02 || modulus.size == 0 ||
      !ReadDerItem(sequence.data, sequence.size, &child, &exponent) ||
      exponent.tag != 0x02 || exponent.size == 0 || child != sequence.size) {
    return false;
  }
  const std::uint8_t* value = modulus.data;
  std::size_t value_size = modulus.size;
  if (value[0] == 0) {
    if (value_size < 2 || (value[1] & 0x80) == 0) return false;
    ++value;
    --value_size;
  } else if ((value[0] & 0x80) != 0) {
    return false;
  }
  unsigned leading_zero_bits = 0;
  for (std::uint8_t mask = 0x80; (value[0] & mask) == 0; mask >>= 1)
    ++leading_zero_bits;
  const std::size_t bit_count = value_size * 8 - leading_zero_bits;
  if (bit_count > static_cast<std::size_t>(INT_MAX)) return false;
  *bits = static_cast<int>(bit_count);
  return *bits >= 2048 && *bits <= 8192;
}

SecKeyRef CreateCrxPublicKey(const std::string& public_key_der, bool ecdsa) {
  static constexpr std::uint8_t kRsaOid[] = {
      0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01};
  static constexpr std::uint8_t kEcPublicKeyOid[] = {
      0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01};
  static constexpr std::uint8_t kPrime256v1Oid[] = {
      0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
  if (public_key_der.empty() || public_key_der.size() > 8192) return nullptr;
  const auto* bytes =
      reinterpret_cast<const std::uint8_t*>(public_key_der.data());
  std::size_t offset = 0;
  DerItem outer;
  if (!ReadDerItem(bytes, public_key_der.size(), &offset, &outer) ||
      outer.tag != 0x30 || offset != public_key_der.size()) {
    return nullptr;
  }
  std::size_t child = 0;
  DerItem algorithm, bit_string;
  if (!ReadDerItem(outer.data, outer.size, &child, &algorithm) ||
      algorithm.tag != 0x30 ||
      !ReadDerItem(outer.data, outer.size, &child, &bit_string) ||
      bit_string.tag != 0x03 || bit_string.size < 2 ||
      child != outer.size || bit_string.data[0] != 0) {
    return nullptr;
  }
  std::size_t algorithm_offset = 0;
  DerItem algorithm_oid;
  if (!ReadDerItem(algorithm.data, algorithm.size, &algorithm_offset,
                   &algorithm_oid)) {
    return nullptr;
  }

  const std::uint8_t* key_bytes = bit_string.data + 1;
  const std::size_t key_size = bit_string.size - 1;
  int key_bits = 0;
  if (ecdsa) {
    DerItem curve_oid;
    if (!OidEquals(algorithm_oid, kEcPublicKeyOid, sizeof(kEcPublicKeyOid)) ||
        !ReadDerItem(algorithm.data, algorithm.size, &algorithm_offset,
                     &curve_oid) ||
        !OidEquals(curve_oid, kPrime256v1Oid, sizeof(kPrime256v1Oid)) ||
        algorithm_offset != algorithm.size || key_size != 65 ||
        key_bytes[0] != 0x04) {
      return nullptr;
    }
    key_bits = 256;
  } else {
    if (!OidEquals(algorithm_oid, kRsaOid, sizeof(kRsaOid))) return nullptr;
    if (algorithm_offset < algorithm.size) {
      DerItem null_parameters;
      if (!ReadDerItem(algorithm.data, algorithm.size, &algorithm_offset,
                       &null_parameters) ||
          null_parameters.tag != 0x05 || null_parameters.size != 0 ||
          algorithm_offset != algorithm.size) {
        return nullptr;
      }
    }
    if (!RsaBits(key_bytes, key_size, &key_bits)) return nullptr;
  }

  CFDataRef key_data = CFDataCreate(kCFAllocatorDefault, key_bytes,
                                    static_cast<CFIndex>(key_size));
  if (!key_data) return nullptr;
  const int bits = key_bits;
  CFNumberRef key_size_number =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &bits);
  if (!key_size_number) {
    CFRelease(key_data);
    return nullptr;
  }
  const void* keys[] = {kSecAttrKeyType, kSecAttrKeyClass,
                        kSecAttrKeySizeInBits};
  const void* values[] = {
      ecdsa ? kSecAttrKeyTypeECSECPrimeRandom : kSecAttrKeyTypeRSA,
      kSecAttrKeyClassPublic, key_size_number};
  CFDictionaryRef attributes = CFDictionaryCreate(
      kCFAllocatorDefault, keys, values, 3, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  CFRelease(key_size_number);
  if (!attributes) {
    CFRelease(key_data);
    return nullptr;
  }
  CFErrorRef error = nullptr;
  SecKeyRef key = SecKeyCreateWithData(key_data, attributes, &error);
  if (error) CFRelease(error);
  CFRelease(attributes);
  CFRelease(key_data);
  return key;
}

}  // namespace

bool VerifySignatureDigest(const std::string& public_key_der, bool ecdsa,
                           const std::string& digest,
                           const std::string& signature) {
  if (public_key_der.empty() || public_key_der.size() > 8192 ||
      digest.size() != CC_SHA256_DIGEST_LENGTH || signature.empty() ||
      signature.size() > 8192) {
    return false;
  }

  SecKeyRef key = CreateCrxPublicKey(public_key_der, ecdsa);
  if (!key) return false;
  const SecKeyAlgorithm algorithm =
      ecdsa ? kSecKeyAlgorithmECDSASignatureDigestX962SHA256
            : kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256;
  if (!SecKeyIsAlgorithmSupported(key, kSecKeyOperationTypeVerify, algorithm) ||
      (!ecdsa && signature.size() != SecKeyGetBlockSize(key))) {
    CFRelease(key);
    return false;
  }

  CFDataRef digest_data = CFDataCreate(
      kCFAllocatorDefault,
      reinterpret_cast<const UInt8*>(digest.data()),
      static_cast<CFIndex>(digest.size()));
  CFDataRef signature_data = CFDataCreate(
      kCFAllocatorDefault,
      reinterpret_cast<const UInt8*>(signature.data()),
      static_cast<CFIndex>(signature.size()));
  if (!digest_data || !signature_data) {
    if (digest_data) CFRelease(digest_data);
    if (signature_data) CFRelease(signature_data);
    CFRelease(key);
    return false;
  }
  CFErrorRef error = nullptr;
  const bool verified = SecKeyVerifySignature(
      key, algorithm, digest_data, signature_data, &error);
  if (error) CFRelease(error);
  CFRelease(signature_data);
  CFRelease(digest_data);
  CFRelease(key);
  return verified;
}

}  // namespace crypto
}  // namespace crx3
}  // namespace shelter
