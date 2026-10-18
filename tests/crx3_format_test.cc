// Тестирует только границы и protobuf-структуру CRX3, без платформенной криптографии.
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>

#include "src/crx3_format.h"

namespace {

void AppendVarint(std::string* out, std::uint64_t value) {
  while (value >= 0x80) {
    out->push_back(static_cast<char>((value & 0x7f) | 0x80));
    value >>= 7;
  }
  out->push_back(static_cast<char>(value));
}

std::string BytesField(std::uint32_t field, const std::string& value) {
  std::string out;
  AppendVarint(&out, (static_cast<std::uint64_t>(field) << 3) | 2);
  AppendVarint(&out, value.size());
  out += value;
  return out;
}

void AppendLe32(std::string* out, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    out->push_back(static_cast<char>(value >> shift));
}

std::string BuildPackage(const std::string& header) {
  std::string bytes = "Cr24";
  AppendLe32(&bytes, 3);
  AppendLe32(&bytes, static_cast<std::uint32_t>(header.size()));
  bytes += header;
  bytes.append("PK\x03\x04zip", 7);
  return bytes;
}

std::string BuildHeader(const std::string& signed_data,
                        const std::string& public_key = "public-key",
                        const std::string& signature = "signature") {
  const std::string proof = BytesField(1, public_key) + BytesField(2, signature);
  return BytesField(2, proof) + BytesField(10000, signed_data);
}

}  // namespace

int main() {
  const std::string crx_id(16, 'i');
  const std::string signed_data = BytesField(1, crx_id);
  const std::string header = BuildHeader(signed_data);
  const std::string bytes = BuildPackage(header);

  shelter::crx3::Package parsed;
  assert(shelter::crx3::Parse(bytes, &parsed));
  assert(parsed.zip_offset == 12 + header.size());
  assert(parsed.signed_header_data == signed_data);
  assert(parsed.crx_id == crx_id);
  assert(parsed.proofs.size() == 1);
  assert(!parsed.proofs[0].ecdsa);
  assert(parsed.proofs[0].public_key == "public-key");
  assert(parsed.proofs[0].signature == "signature");

  // Версия, границы заголовка и обязательные поля проверяются строго.
  std::string wrong_version = bytes;
  wrong_version[4] = 2;
  assert(!shelter::crx3::Parse(wrong_version, &parsed));
  std::string truncated = bytes;
  truncated.resize(12 + header.size() - 1);
  assert(!shelter::crx3::Parse(truncated, &parsed));
  std::string missing_zip = bytes.substr(0, 12 + header.size());
  assert(!shelter::crx3::Parse(missing_zip, &parsed));
  std::string no_proof = BuildPackage(BytesField(10000, signed_data));
  assert(!shelter::crx3::Parse(no_proof, &parsed));
  std::string no_signed_data = BuildPackage(BytesField(2, BytesField(1, "k") +
                                                           BytesField(2, "s")));
  assert(!shelter::crx3::Parse(no_signed_data, &parsed));

  // Повторные ключи и CRX ID не допускаются; повреждённые varint отвергаются.
  const std::string proof = BytesField(2, BytesField(1, "public-key") +
                                              BytesField(2, "signature"));
  std::string duplicate_proof_header = proof + proof +
                                       BytesField(10000, signed_data);
  assert(!shelter::crx3::Parse(BuildPackage(duplicate_proof_header), &parsed));
  std::string duplicate_id = BytesField(1, crx_id) + BytesField(1, crx_id);
  assert(!shelter::crx3::Parse(BuildPackage(BuildHeader(duplicate_id)), &parsed));
  std::string bad_header("\x80", 1);
  assert(!shelter::crx3::Parse(BuildPackage(bad_header), &parsed));

  return 0;
}
