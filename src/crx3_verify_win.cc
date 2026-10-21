// SHELTER — проверка подписей CRX3 через BCrypt/CNG.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "src/crx3_crypto.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <memory>
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
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::vector<UCHAR> object;
  bool ok = false;
  DWORD object_size = 0;
  DWORD result_size = 0;
  DWORD returned = 0;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  0) != 0 ||
      BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&object_size),
                        sizeof(object_size), &returned, 0) != 0 ||
      BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                        reinterpret_cast<PUCHAR>(&result_size),
                        sizeof(result_size), &returned, 0) != 0 ||
      result_size != 32) {
    goto done;
  }
  object.resize(object_size);
  if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0,
                       0) != 0) {
    goto done;
  }
  for (const auto& chunk : chunks) {
    if (chunk.second && !chunk.first) goto done;
    std::size_t offset = 0;
    while (offset < chunk.second) {
      const ULONG count = static_cast<ULONG>(std::min<std::size_t>(
          chunk.second - offset, static_cast<std::size_t>(ULONG_MAX)));
      if (BCryptHashData(hash,
                         reinterpret_cast<PUCHAR>(
                             const_cast<char*>(chunk.first + offset)),
                         count, 0) != 0) {
        goto done;
      }
      offset += count;
    }
  }
  digest->assign(result_size, '\0');
  if (BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(digest->data()),
                       result_size, 0) != 0) {
    digest->clear();
    goto done;
  }
  ok = true;

done:
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  if (!ok) digest->clear();
  return ok;
}

bool Sha256(const std::string& bytes, std::string* digest) {
  return Sha256Chunks({{bytes.data(), bytes.size()}}, digest);
}

namespace {

bool EcdsaDerToRaw(const std::string& der, std::array<UCHAR, 64>* raw) {
  if (!raw || der.size() < 8 || der.size() > 80 ||
      static_cast<UCHAR>(der[0]) != 0x30) {
    return false;
  }
  std::size_t offset = 1;
  const auto read_length = [&der](std::size_t* cursor,
                                  std::size_t* length) -> bool {
    if (!cursor || !length || *cursor >= der.size()) return false;
    const UCHAR first = static_cast<UCHAR>(der[(*cursor)++]);
    if ((first & 0x80) == 0) {
      *length = first;
      return true;
    }
    const std::size_t count = first & 0x7f;
    if (count == 0 || count > 2 || count > der.size() - *cursor ||
        static_cast<UCHAR>(der[*cursor]) == 0) {
      return false;
    }
    std::size_t value = 0;
    for (std::size_t i = 0; i < count; ++i)
      value = (value << 8) | static_cast<UCHAR>(der[(*cursor)++]);
    if (value < 128) return false;
    *length = value;
    return true;
  };
  std::size_t sequence_size = 0;
  if (!read_length(&offset, &sequence_size) ||
      sequence_size != der.size() - offset) {
    return false;
  }
  raw->fill(0);
  for (std::size_t component = 0; component < 2; ++component) {
    if (offset >= der.size() || static_cast<UCHAR>(der[offset++]) != 0x02)
      return false;
    std::size_t integer_size = 0;
    if (!read_length(&offset, &integer_size) || integer_size == 0 ||
        integer_size > der.size() - offset) {
      return false;
    }
    const UCHAR* integer =
        reinterpret_cast<const UCHAR*>(der.data() + offset);
    if ((integer[0] & 0x80) != 0) return false;
    if (integer_size > 1 && integer[0] == 0) {
      if ((integer[1] & 0x80) == 0) return false;
      ++integer;
      --integer_size;
    }
    if (integer_size > 32) return false;
    const std::size_t destination = component * 32 + (32 - integer_size);
    std::copy(integer, integer + integer_size, raw->begin() + destination);
    offset += static_cast<std::size_t>(
                  integer - reinterpret_cast<const UCHAR*>(
                                der.data() + offset)) +
              integer_size;
  }
  return offset == der.size();
}

}  // namespace

bool VerifySignatureDigest(const std::string& public_key_der, bool ecdsa,
                           const std::string& digest,
                           const std::string& signature) {
  if (public_key_der.empty() || public_key_der.size() > 8192 ||
      digest.size() != 32 || signature.empty() || signature.size() > 8192 ||
      public_key_der.size() > static_cast<std::size_t>(ULONG_MAX)) {
    return false;
  }

  DWORD info_size = 0;
  constexpr DWORD kEncoding = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
  void* decoded = nullptr;
  if (!CryptDecodeObjectEx(
          kEncoding, X509_PUBLIC_KEY_INFO,
          reinterpret_cast<const BYTE*>(public_key_der.data()),
          static_cast<DWORD>(public_key_der.size()), CRYPT_DECODE_ALLOC_FLAG,
          nullptr, &decoded, &info_size) ||
      !decoded) {
    return false;
  }
  std::unique_ptr<CERT_PUBLIC_KEY_INFO, decltype(&LocalFree)> info_owner(
      static_cast<CERT_PUBLIC_KEY_INFO*>(decoded), &LocalFree);
  CERT_PUBLIC_KEY_INFO* info = info_owner.get();
  const char* const algorithm_oid = info->Algorithm.pszObjId;
  BCRYPT_KEY_HANDLE key = nullptr;
  if (ecdsa) {
    static constexpr BYTE kPrime256v1Oid[] = {
        0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
    const CRYPT_OBJID_BLOB& parameters = info->Algorithm.Parameters;
    if (!algorithm_oid || strcmp(algorithm_oid, "1.2.840.10045.2.1") != 0 ||
        parameters.cbData != sizeof(kPrime256v1Oid) || !parameters.pbData ||
        memcmp(parameters.pbData, kPrime256v1Oid,
               sizeof(kPrime256v1Oid)) != 0) {
      return false;
    }
  } else if (!algorithm_oid ||
             strcmp(algorithm_oid, "1.2.840.113549.1.1.1") != 0) {
    return false;
  }
  if (!CryptImportPublicKeyInfoEx2(kEncoding, info, 0, nullptr, &key) || !key)
    return false;

  DWORD key_bits = 0;
  DWORD returned = 0;
  bool verified = false;
  if (BCryptGetProperty(key, BCRYPT_KEY_LENGTH,
                        reinterpret_cast<PUCHAR>(&key_bits), sizeof(key_bits),
                        &returned, 0) == 0 &&
      returned == sizeof(key_bits) &&
      ((!ecdsa && key_bits >= 2048 && key_bits <= 8192) ||
       (ecdsa && key_bits == 256))) {
    if (ecdsa) {
      std::array<UCHAR, 64> raw_signature{};
      if (EcdsaDerToRaw(signature, &raw_signature)) {
        verified = BCryptVerifySignature(
                       key, nullptr,
                       reinterpret_cast<PUCHAR>(
                           const_cast<char*>(digest.data())),
                       static_cast<ULONG>(digest.size()), raw_signature.data(),
                       static_cast<ULONG>(raw_signature.size()), 0) == 0;
      }
    } else if (signature.size() == (key_bits + 7u) / 8u) {
      BCRYPT_PKCS1_PADDING_INFO padding = {BCRYPT_SHA256_ALGORITHM};
      verified = BCryptVerifySignature(
                     key, &padding,
                     reinterpret_cast<PUCHAR>(
                         const_cast<char*>(digest.data())),
                     static_cast<ULONG>(digest.size()),
                     reinterpret_cast<PUCHAR>(
                         const_cast<char*>(signature.data())),
                     static_cast<ULONG>(signature.size()),
                     BCRYPT_PAD_PKCS1) == 0;
    }
  }
  BCryptDestroyKey(key);
  return verified;
}

}  // namespace crypto
}  // namespace crx3
}  // namespace shelter
