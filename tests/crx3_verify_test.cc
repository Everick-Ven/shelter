// Проверяет реальные подписи CRX3 через системный API текущей платформы.
#include "src/crx3.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

int failures = 0;

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

void Check(bool condition, const char* label) {
  if (condition) return;
  std::cerr << "FAIL: " << label << '\n';
  ++failures;
}

void CheckValid(const std::filesystem::path& directory, const char* filename,
                const std::string& expected_id) {
  const std::string bytes = ReadFile(directory / filename);
  std::size_t zip_offset = 0;
  std::string extension_id;
  const bool verified =
      !bytes.empty() && shelter::crx3::Verify(bytes, "", &zip_offset,
                                              &extension_id);
  Check(verified, filename);
  if (!verified) return;
  Check(extension_id == expected_id, "derived extension ID matches fixture");
  Check(zip_offset < bytes.size() &&
            bytes.compare(zip_offset, 4, "PK\x03\x04", 4) == 0,
        "verified ZIP offset points to the archive");
}

void CheckRejected(const std::filesystem::path& directory,
                   const char* filename) {
  const std::string bytes = ReadFile(directory / filename);
  Check(!bytes.empty(), "negative fixture exists");
  if (bytes.empty()) return;
  std::size_t zip_offset = 73;
  std::string extension_id = "unchanged";
  const bool verified =
      shelter::crx3::Verify(bytes, "", &zip_offset, &extension_id);
  Check(!verified, filename);
  Check(zip_offset == 73 && extension_id == "unchanged",
        "failed verification does not publish output parameters");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Expected the CRX3 fixture directory\n";
    return 2;
  }
  const std::filesystem::path fixtures = std::filesystem::u8path(argv[1]);
  constexpr char kRsaId[] = "ipkpbpbhakaonmdpngcndaofgafmcbbg";
  constexpr char kEcdsaId[] = "popafaiaemohlkiabjdglhonfbmoiooc";
  constexpr char kKnownCrxViewerId[] = "jifpbeccnghkjeaalbbjmodiffmgedin";

  CheckValid(fixtures, "rsa_valid.crx", kRsaId);
  CheckValid(fixtures, "ecdsa_valid.crx", kEcdsaId);
  CheckValid(fixtures, "multi_proof_foreign_key.crx", kRsaId);

  CheckRejected(fixtures, "bad_zip_byte.crx");
  CheckRejected(fixtures, "bad_signed_header_data.crx");
  CheckRejected(fixtures, "bad_crx_id.crx");
  CheckRejected(fixtures, "bad_context_without_nul.crx");

  const std::string rsa_bytes = ReadFile(fixtures / "rsa_valid.crx");
  std::size_t zip_offset = 0;
  std::string extension_id;
  Check(!rsa_bytes.empty() &&
            !shelter::crx3::Verify(rsa_bytes, kEcdsaId, &zip_offset,
                                   &extension_id),
        "incorrect expected store ID is rejected");

  const std::string known_key =
      ReadFile(fixtures / "crxviewer_1.6.10_public_key.der");
  std::string known_id;
  Check(!known_key.empty() &&
            shelter::crx3::ExtensionIdFromPublicKeyDer(known_key, &known_id),
        "known Chrome extension public key is parsed for ID derivation");
  Check(known_id == kKnownCrxViewerId,
        "known CRX Viewer key maps to its published extension ID");

  if (failures) {
    std::cerr << failures << " CRX3 signature regression check(s) failed\n";
    return 1;
  }
  std::cout << "SHELTER_CRX3_PLATFORM_VERIFY_PASS\n";
  return 0;
}
