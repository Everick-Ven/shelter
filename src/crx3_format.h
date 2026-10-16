// SHELTER — разбор формата заголовка CRX3 без проверки криптографии.
#ifndef SHELTER_CRX3_FORMAT_H_
#define SHELTER_CRX3_FORMAT_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace shelter {
namespace crx3 {

struct Proof {
  bool ecdsa = false;
  std::string public_key;
  std::string signature;
};

struct Package {
  std::size_t zip_offset = 0;
  std::string signed_header_data;
  std::string crx_id;
  std::vector<Proof> proofs;
};

namespace detail {

inline bool ReadVarint(const std::string& bytes, std::size_t* offset,
                       std::uint64_t* value) {
  if (!offset || !value) return false;
  std::uint64_t out = 0;
  for (unsigned i = 0; i < 10; ++i) {
    if (*offset >= bytes.size()) return false;
    const std::uint8_t byte = static_cast<std::uint8_t>(bytes[(*offset)++]);
    if (i == 9 && byte > 1) return false;
    out |= static_cast<std::uint64_t>(byte & 0x7f) << (7 * i);
    if ((byte & 0x80) == 0) {
      *value = out;
      return true;
    }
  }
  return false;
}

inline bool ReadFieldBytes(const std::string& bytes, std::size_t* offset,
                           std::string* value) {
  if (!offset || !value) return false;
  std::uint64_t length = 0;
  if (!ReadVarint(bytes, offset, &length) || length > bytes.size() - *offset)
    return false;
  const std::size_t count = static_cast<std::size_t>(length);
  value->assign(bytes.data() + *offset, count);
  *offset += count;
  return true;
}

inline bool SkipField(const std::string& bytes, std::size_t* offset,
                      unsigned wire_type) {
  std::uint64_t value = 0;
  switch (wire_type) {
    case 0:
      return ReadVarint(bytes, offset, &value);
    case 1:
      if (bytes.size() - *offset < 8) return false;
      *offset += 8;
      return true;
    case 2:
      if (!ReadVarint(bytes, offset, &value) || value > bytes.size() - *offset)
        return false;
      *offset += static_cast<std::size_t>(value);
      return true;
    case 5:
      if (bytes.size() - *offset < 4) return false;
      *offset += 4;
      return true;
    default:
      return false;
  }
}

inline bool ParseProof(const std::string& encoded, bool ecdsa, Proof* proof) {
  if (!proof || encoded.empty() || encoded.size() > 16u * 1024u) return false;
  Proof parsed;
  parsed.ecdsa = ecdsa;
  bool has_key = false;
  bool has_signature = false;
  std::size_t offset = 0;
  while (offset < encoded.size()) {
    std::uint64_t tag = 0;
    if (!ReadVarint(encoded, &offset, &tag) || (tag >> 3) == 0) return false;
    const unsigned field = static_cast<unsigned>(tag >> 3);
    const unsigned wire = static_cast<unsigned>(tag & 7);
    if (field == 1 || field == 2) {
      if (wire != 2 || (field == 1 ? has_key : has_signature)) return false;
      std::string value;
      if (!ReadFieldBytes(encoded, &offset, &value) || value.empty() ||
          value.size() > 8192) {
        return false;
      }
      if (field == 1) {
        parsed.public_key = std::move(value);
        has_key = true;
      } else {
        parsed.signature = std::move(value);
        has_signature = true;
      }
    } else if (!SkipField(encoded, &offset, wire)) {
      return false;
    }
  }
  if (!has_key || !has_signature) return false;
  *proof = std::move(parsed);
  return true;
}

inline bool ParseSignedData(const std::string& encoded, std::string* crx_id) {
  if (!crx_id || encoded.empty() || encoded.size() > 4096) return false;
  bool has_id = false;
  std::size_t offset = 0;
  while (offset < encoded.size()) {
    std::uint64_t tag = 0;
    if (!ReadVarint(encoded, &offset, &tag) || (tag >> 3) == 0) return false;
    const unsigned field = static_cast<unsigned>(tag >> 3);
    const unsigned wire = static_cast<unsigned>(tag & 7);
    if (field == 1) {
      if (wire != 2 || has_id) return false;
      if (!ReadFieldBytes(encoded, &offset, crx_id) || crx_id->size() != 16)
        return false;
      has_id = true;
    } else if (!SkipField(encoded, &offset, wire)) {
      return false;
    }
  }
  return has_id;
}

}  // namespace detail

// Проверяет только структуру CRX3. Криптографическая проверка выполняется
// отдельным системным API в platform_win.cc / platform_mac.mm.
inline bool Parse(const std::string& bytes, Package* package) {
  if (!package || bytes.size() < 12 || bytes.compare(0, 4, "Cr24") != 0)
    return false;
  const auto read_le32 = [&bytes](std::size_t offset) -> std::uint32_t {
    return static_cast<std::uint8_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 3])) << 24);
  };
  if (read_le32(4) != 3) return false;
  const std::size_t header_size = read_le32(8);
  constexpr std::size_t kMaxHeaderBytes = 1024u * 1024u;
  if (header_size == 0 || header_size > kMaxHeaderBytes ||
      header_size > bytes.size() - 12 || 12 + header_size >= bytes.size()) {
    return false;
  }

  const std::string header(bytes.data() + 12, header_size);
  Package parsed;
  parsed.zip_offset = 12 + header_size;
  std::string signed_data;
  bool has_signed_data = false;
  std::size_t offset = 0;
  while (offset < header.size()) {
    std::uint64_t tag = 0;
    if (!detail::ReadVarint(header, &offset, &tag) || (tag >> 3) == 0)
      return false;
    const std::uint64_t field_number = tag >> 3;
    const unsigned wire = static_cast<unsigned>(tag & 7);
    if (field_number == 2 || field_number == 3 || field_number == 10000) {
      if (wire != 2) return false;
      std::string value;
      if (!detail::ReadFieldBytes(header, &offset, &value)) return false;
      if (field_number == 10000) {
        if (has_signed_data) return false;
        signed_data = std::move(value);
        has_signed_data = true;
      } else {
        if (parsed.proofs.size() >= 16) return false;
        Proof proof;
        if (!detail::ParseProof(value, field_number == 3, &proof)) return false;
        for (const Proof& existing : parsed.proofs) {
          if (existing.ecdsa == proof.ecdsa &&
              existing.public_key == proof.public_key) {
            return false;
          }
        }
        parsed.proofs.push_back(std::move(proof));
      }
    } else if (!detail::SkipField(header, &offset, wire)) {
      return false;
    }
  }
  if (!has_signed_data || parsed.proofs.empty() ||
      !detail::ParseSignedData(signed_data, &parsed.crx_id)) {
    return false;
  }
  parsed.signed_header_data = std::move(signed_data);
  *package = std::move(parsed);
  return true;
}

}  // namespace crx3
}  // namespace shelter

#endif  // SHELTER_CRX3_FORMAT_H_
