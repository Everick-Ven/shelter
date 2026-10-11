// SHELTER — byte-compatible native implementation of the legacy ss1 format.
#include "src/secret_crypto.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "src/platform.h"

namespace shelter {
namespace secret_crypto {
namespace {

using Byte = std::uint8_t;
constexpr size_t kKeyBytes = 32;
constexpr size_t kNonceBytes = 12;
constexpr size_t kTagBytes = 32;
constexpr size_t kMaxPayloadBytes = 64u * 1024u * 1024u;

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

void Wipe(void* data, size_t size) {
  volatile Byte* p = static_cast<volatile Byte*>(data);
  while (size--) *p++ = 0;
}

std::uint32_t ReadBe32(const Byte* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) |
         static_cast<std::uint32_t>(p[3]);
}

void WriteBe32(Byte* p, std::uint32_t value) {
  p[0] = static_cast<Byte>(value >> 24);
  p[1] = static_cast<Byte>(value >> 16);
  p[2] = static_cast<Byte>(value >> 8);
  p[3] = static_cast<Byte>(value);
}

std::uint32_t RotR(std::uint32_t x, unsigned n) {
  return (x >> n) | (x << (32 - n));
}

std::array<Byte, 32> Sha256(const Byte* input, size_t length) {
  std::array<std::uint32_t, 8> h = {
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  const size_t total = ((length + 9u + 63u) / 64u) * 64u;
  std::vector<Byte> padded(total, 0);
  if (length) std::copy(input, input + length, padded.begin());
  padded[length] = 0x80;
  const std::uint64_t bit_length = static_cast<std::uint64_t>(length) * 8u;
  for (size_t i = 0; i < 8; ++i)
    padded[total - 1u - i] = static_cast<Byte>(bit_length >> (8u * i));

  std::array<std::uint32_t, 64> w{};
  for (size_t offset = 0; offset < total; offset += 64) {
    for (size_t i = 0; i < 16; ++i)
      w[i] = ReadBe32(padded.data() + offset + i * 4);
    for (size_t i = 16; i < 64; ++i) {
      const std::uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^
                               (w[i - 15] >> 3);
      const std::uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^
                               (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (size_t i = 0; i < 64; ++i) {
      const std::uint32_t s1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
      const std::uint32_t choose = (e & f) ^ (~e & g);
      const std::uint32_t t1 = hh + s1 + choose + kSha256K[i] + w[i];
      const std::uint32_t s0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t t2 = s0 + majority;
      hh = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }

  std::array<Byte, 32> digest{};
  for (size_t i = 0; i < h.size(); ++i) WriteBe32(digest.data() + i * 4, h[i]);
  Wipe(padded.data(), padded.size());
  Wipe(w.data(), w.size() * sizeof(w[0]));
  Wipe(h.data(), h.size() * sizeof(h[0]));
  return digest;
}

std::array<Byte, 32> Sha256(const std::vector<Byte>& input) {
  return Sha256(input.data(), input.size());
}

std::array<Byte, 32> HmacSha256(const std::vector<Byte>& key,
                                const std::vector<Byte>& data) {
  std::vector<Byte> normalized = key;
  if (normalized.size() > 64) {
    const auto digest = Sha256(normalized);
    normalized.assign(digest.begin(), digest.end());
  }
  std::array<Byte, 64> inner_pad{}, outer_pad{};
  for (size_t i = 0; i < inner_pad.size(); ++i) {
    const Byte k = i < normalized.size() ? normalized[i] : 0;
    inner_pad[i] = k ^ 0x36;
    outer_pad[i] = k ^ 0x5c;
  }
  std::vector<Byte> inner;
  inner.reserve(inner_pad.size() + data.size());
  inner.insert(inner.end(), inner_pad.begin(), inner_pad.end());
  inner.insert(inner.end(), data.begin(), data.end());
  const auto inner_hash = Sha256(inner);
  std::vector<Byte> outer;
  outer.reserve(outer_pad.size() + inner_hash.size());
  outer.insert(outer.end(), outer_pad.begin(), outer_pad.end());
  outer.insert(outer.end(), inner_hash.begin(), inner_hash.end());
  const auto result = Sha256(outer);
  Wipe(normalized.data(), normalized.size());
  Wipe(inner.data(), inner.size());
  Wipe(outer.data(), outer.size());
  Wipe(inner_pad.data(), inner_pad.size());
  Wipe(outer_pad.data(), outer_pad.size());
  return result;
}

bool DecodeHexKey(const std::string& hex, std::vector<Byte>* key) {
  if (!key || hex.size() != kKeyBytes * 2) return false;
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  key->resize(kKeyBytes);
  for (size_t i = 0; i < key->size(); ++i) {
    const int hi = digit(hex[i * 2]), lo = digit(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      key->clear();
      return false;
    }
    (*key)[i] = static_cast<Byte>((hi << 4) | lo);
  }
  return true;
}

std::array<Byte, 32> DeriveKey(const std::vector<Byte>& master,
                               const char* label) {
  const std::string text(label);
  const std::vector<Byte> data(text.begin(), text.end());
  return HmacSha256(master, data);
}

void QuarterRound(std::array<std::uint32_t, 16>* x, size_t a, size_t b,
                  size_t c, size_t d) {
  auto& s = *x;
  s[a] += s[b]; s[d] = (s[d] ^ s[a]) << 16 | (s[d] ^ s[a]) >> 16;
  s[c] += s[d]; s[b] = (s[b] ^ s[c]) << 12 | (s[b] ^ s[c]) >> 20;
  s[a] += s[b]; s[d] = (s[d] ^ s[a]) << 8 | (s[d] ^ s[a]) >> 24;
  s[c] += s[d]; s[b] = (s[b] ^ s[c]) << 7 | (s[b] ^ s[c]) >> 25;
}

bool ChaCha20(const std::vector<Byte>& key, const Byte* nonce,
              const std::vector<Byte>& input, std::vector<Byte>* output) {
  if (!output || key.size() != kKeyBytes || !nonce ||
      input.size() > kMaxPayloadBytes) return false;
  const std::uint64_t blocks =
      (static_cast<std::uint64_t>(input.size()) + 63u) / 64u;
  if (blocks > std::numeric_limits<std::uint32_t>::max() - 1u) return false;

  std::array<std::uint32_t, 16> initial = {
      0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u};
  for (size_t i = 0; i < 8; ++i) {
    const Byte* p = key.data() + i * 4;
    initial[4 + i] = static_cast<std::uint32_t>(p[0]) |
                     (static_cast<std::uint32_t>(p[1]) << 8) |
                     (static_cast<std::uint32_t>(p[2]) << 16) |
                     (static_cast<std::uint32_t>(p[3]) << 24);
  }
  for (size_t i = 0; i < 3; ++i) {
    const Byte* p = nonce + i * 4;
    initial[13 + i] = static_cast<std::uint32_t>(p[0]) |
                      (static_cast<std::uint32_t>(p[1]) << 8) |
                      (static_cast<std::uint32_t>(p[2]) << 16) |
                      (static_cast<std::uint32_t>(p[3]) << 24);
  }

  output->resize(input.size());
  std::uint32_t counter = 1;
  for (size_t offset = 0; offset < input.size(); offset += 64, ++counter) {
    initial[12] = counter;
    std::array<std::uint32_t, 16> x = initial;
    for (unsigned round = 0; round < 10; ++round) {
      QuarterRound(&x, 0, 4, 8, 12); QuarterRound(&x, 1, 5, 9, 13);
      QuarterRound(&x, 2, 6, 10, 14); QuarterRound(&x, 3, 7, 11, 15);
      QuarterRound(&x, 0, 5, 10, 15); QuarterRound(&x, 1, 6, 11, 12);
      QuarterRound(&x, 2, 7, 8, 13); QuarterRound(&x, 3, 4, 9, 14);
    }
    const size_t count = std::min<size_t>(64, input.size() - offset);
    for (size_t i = 0; i < count; ++i) {
      const std::uint32_t word = x[i / 4] + initial[i / 4];
      const Byte stream = static_cast<Byte>(word >> ((i % 4) * 8));
      (*output)[offset + i] = input[offset + i] ^ stream;
    }
    Wipe(x.data(), x.size() * sizeof(x[0]));
  }
  Wipe(initial.data(), initial.size() * sizeof(initial[0]));
  return true;
}

std::string Base64Encode(const std::vector<Byte>& bytes) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((bytes.size() + 2) / 3) * 4);
  for (size_t i = 0; i < bytes.size(); i += 3) {
    const std::uint32_t a = bytes[i];
    const std::uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
    const std::uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
    const std::uint32_t word = (a << 16) | (b << 8) | c;
    out.push_back(kAlphabet[(word >> 18) & 63]);
    out.push_back(kAlphabet[(word >> 12) & 63]);
    out.push_back(i + 1 < bytes.size() ? kAlphabet[(word >> 6) & 63] : '=');
    out.push_back(i + 2 < bytes.size() ? kAlphabet[word & 63] : '=');
  }
  return out;
}

bool Base64Decode(const std::string& text, std::vector<Byte>* out) {
  if (!out || text.empty() || (text.size() % 4) != 0) return false;
  auto value = [](unsigned char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  out->clear();
  out->reserve((text.size() / 4) * 3);
  for (size_t i = 0; i < text.size(); i += 4) {
    const bool last = i + 4 == text.size();
    const int a = value(static_cast<unsigned char>(text[i]));
    const int b = value(static_cast<unsigned char>(text[i + 1]));
    const bool pad2 = text[i + 2] == '=';
    const bool pad3 = text[i + 3] == '=';
    const int c = pad2 ? 0 : value(static_cast<unsigned char>(text[i + 2]));
    const int d = pad3 ? 0 : value(static_cast<unsigned char>(text[i + 3]));
    if (a < 0 || b < 0 || c < 0 || d < 0 || (pad2 && !pad3) ||
        ((pad2 || pad3) && !last)) {
      out->clear();
      return false;
    }
    const std::uint32_t word = (static_cast<std::uint32_t>(a) << 18) |
                               (static_cast<std::uint32_t>(b) << 12) |
                               (static_cast<std::uint32_t>(c) << 6) |
                               static_cast<std::uint32_t>(d);
    out->push_back(static_cast<Byte>(word >> 16));
    if (!pad2) out->push_back(static_cast<Byte>(word >> 8));
    if (!pad3) out->push_back(static_cast<Byte>(word));
  }
  return true;
}

std::vector<Byte> AsBytes(const std::string& text) {
  return std::vector<Byte>(text.begin(), text.end());
}

bool ConstantTimeEqual(const Byte* a, const Byte* b, size_t size) {
  Byte diff = 0;
  for (size_t i = 0; i < size; ++i) diff |= a[i] ^ b[i];
  return diff == 0;
}

}  // namespace

bool EncryptWithKey(const std::string& key_hex, const std::string& plaintext,
                    const std::string& nonce, std::string* ciphertext) {
  if (!ciphertext) return false;
  ciphertext->clear();
  if (plaintext.size() > kMaxPayloadBytes || nonce.size() != kNonceBytes)
    return false;

  std::vector<Byte> master;
  if (!DecodeHexKey(key_hex, &master)) return false;
  auto enc_key = DeriveKey(master, "shelter/enc");
  auto mac_key = DeriveKey(master, "shelter/mac");
  const std::vector<Byte> nonce_bytes(nonce.begin(), nonce.end());
  std::vector<Byte> clear = AsBytes(plaintext);
  std::vector<Byte> encrypted;
  std::vector<Byte> encryption_key(enc_key.begin(), enc_key.end());
  if (!ChaCha20(encryption_key, nonce_bytes.data(), clear, &encrypted)) {
    Wipe(clear.data(), clear.size());
    Wipe(master.data(), master.size());
    Wipe(encryption_key.data(), encryption_key.size());
    Wipe(enc_key.data(), enc_key.size());
    Wipe(mac_key.data(), mac_key.size());
    return false;
  }
  Wipe(clear.data(), clear.size());
  Wipe(encryption_key.data(), encryption_key.size());
  std::vector<Byte> authenticated;
  authenticated.reserve(nonce_bytes.size() + encrypted.size());
  authenticated.insert(authenticated.end(), nonce_bytes.begin(), nonce_bytes.end());
  authenticated.insert(authenticated.end(), encrypted.begin(), encrypted.end());
  std::vector<Byte> authentication_key(mac_key.begin(), mac_key.end());
  auto tag = HmacSha256(authentication_key, authenticated);
  Wipe(authentication_key.data(), authentication_key.size());
  std::vector<Byte> packed;
  packed.reserve(authenticated.size() + tag.size());
  packed.insert(packed.end(), authenticated.begin(), authenticated.end());
  packed.insert(packed.end(), tag.begin(), tag.end());
  *ciphertext = "ss1:" + Base64Encode(packed);

  Wipe(master.data(), master.size());
  Wipe(encrypted.data(), encrypted.size());
  Wipe(authenticated.data(), authenticated.size());
  Wipe(packed.data(), packed.size());
  Wipe(enc_key.data(), enc_key.size());
  Wipe(mac_key.data(), mac_key.size());
  Wipe(tag.data(), tag.size());
  return true;
}

bool DecryptWithKey(const std::string& key_hex, const std::string& ciphertext,
                    std::string* plaintext) {
  if (!plaintext) return false;
  plaintext->clear();
  constexpr char kPrefix[] = "ss1:";
  if (ciphertext.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0 ||
      ciphertext.size() > ((kMaxPayloadBytes + kNonceBytes + kTagBytes + 2) / 3) * 4 + sizeof(kPrefix))
    return false;

  std::vector<Byte> packed;
  if (!Base64Decode(ciphertext.substr(sizeof(kPrefix) - 1), &packed) ||
      packed.size() < kNonceBytes + kTagBytes ||
      packed.size() > kNonceBytes + kMaxPayloadBytes + kTagBytes) {
    Wipe(packed.data(), packed.size());
    return false;
  }
  std::vector<Byte> master;
  if (!DecodeHexKey(key_hex, &master)) {
    Wipe(packed.data(), packed.size());
    return false;
  }
  const size_t encrypted_size = packed.size() - kNonceBytes - kTagBytes;
  const Byte* nonce = packed.data();
  const Byte* tag = packed.data() + kNonceBytes + encrypted_size;
  auto enc_key = DeriveKey(master, "shelter/enc");
  auto mac_key = DeriveKey(master, "shelter/mac");
  std::vector<Byte> authenticated(packed.begin(), packed.end() - kTagBytes);
  std::vector<Byte> authentication_key(mac_key.begin(), mac_key.end());
  auto expected = HmacSha256(authentication_key, authenticated);
  Wipe(authentication_key.data(), authentication_key.size());
  if (!ConstantTimeEqual(expected.data(), tag, kTagBytes)) {
    Wipe(master.data(), master.size());
    Wipe(packed.data(), packed.size());
    Wipe(authenticated.data(), authenticated.size());
    Wipe(enc_key.data(), enc_key.size());
    Wipe(mac_key.data(), mac_key.size());
    Wipe(expected.data(), expected.size());
    return false;
  }
  std::vector<Byte> encrypted(packed.begin() + kNonceBytes,
                              packed.begin() + kNonceBytes + encrypted_size);
  std::vector<Byte> clear;
  std::vector<Byte> encryption_key(enc_key.begin(), enc_key.end());
  const bool ok = ChaCha20(encryption_key, nonce, encrypted, &clear);
  if (ok) plaintext->assign(clear.begin(), clear.end());
  Wipe(encryption_key.data(), encryption_key.size());
  Wipe(master.data(), master.size());
  Wipe(packed.data(), packed.size());
  Wipe(authenticated.data(), authenticated.size());
  Wipe(encrypted.data(), encrypted.size());
  Wipe(clear.data(), clear.size());
  Wipe(enc_key.data(), enc_key.size());
  Wipe(mac_key.data(), mac_key.size());
  Wipe(expected.data(), expected.size());
  return ok;
}

bool Encrypt(const std::string& plaintext, std::string* ciphertext) {
  if (!ciphertext) return false;
  ciphertext->clear();
  if (plaintext.size() > kMaxPayloadBytes) return false;
  std::string key_hex, error, nonce;
  if (!platform::SecretKeyHex(&key_hex, &error) ||
      !platform::RandomBytes(kNonceBytes, &nonce)) {
    Wipe(key_hex.data(), key_hex.size());
    return false;
  }
  const bool ok = EncryptWithKey(key_hex, plaintext, nonce, ciphertext);
  Wipe(key_hex.data(), key_hex.size());
  Wipe(nonce.data(), nonce.size());
  return ok;
}

bool Decrypt(const std::string& ciphertext, std::string* plaintext) {
  if (!plaintext) return false;
  plaintext->clear();
  std::string key_hex, error;
  if (!platform::SecretKeyHex(&key_hex, &error)) return false;
  const bool ok = DecryptWithKey(key_hex, ciphertext, plaintext);
  Wipe(key_hex.data(), key_hex.size());
  return ok;
}

}  // namespace secret_crypto
}  // namespace shelter
