#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "src/platform.h"
#include "src/security_paths.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <climits>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace shelter {
namespace platform {

namespace {

std::string Utf8(const std::wstring& w) {
  if (w.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
                              nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr,
                      nullptr);
  return s;
}

std::wstring Wide(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

std::string KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR p = nullptr;
  std::string out;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p)) && p) {
    out = Utf8(p);
    CoTaskMemFree(p);
  }
  return out;
}

// Маска — layered child HWND корневого окна: она следует за окном без хука
// перемещений, а её координаты задаются относительно клиентской области.
constexpr wchar_t kClipMaskClass[] = L"ShelterViewportClipMask";
HWND g_clip_mask_window = nullptr;
HWND g_clip_mask_owner = nullptr;
HWND g_clip_mask_view = nullptr;
int g_clip_x = 0, g_clip_y = 0, g_clip_w = 0, g_clip_h = 0;
double g_clip_radii[4] = {0, 0, 0, 0};
std::uint32_t g_clip_color = 0x0e0f13;
bool g_clip_visible = false;

bool g_clip_drawn_valid = false;
bool g_clip_drawn_visible = false;
HWND g_clip_drawn_owner = nullptr;
int g_clip_drawn_x = 0, g_clip_drawn_y = 0;
int g_clip_drawn_w = 0, g_clip_drawn_h = 0;
UINT g_clip_drawn_dpi = 96;
double g_clip_drawn_radii[4] = {0, 0, 0, 0};
std::uint32_t g_clip_drawn_color = 0;

LRESULT CALLBACK ClipMaskWindowProc(HWND window, UINT message, WPARAM wparam,
                                    LPARAM lparam) {
  switch (message) {
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
      return 0;
    case WM_ERASEBKGND:
      return 1;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

bool EnsureClipMaskWindowClass() {
  static bool registered = false;
  if (registered) return true;
  WNDCLASSEXW cls{};
  cls.cbSize = sizeof(cls);
  cls.lpfnWndProc = ClipMaskWindowProc;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = kClipMaskClass;
  if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return false;
  registered = true;
  return true;
}

void InvalidateClipMaskCache() {
  g_clip_drawn_valid = false;
  g_clip_drawn_visible = false;
  g_clip_drawn_owner = nullptr;
}

bool EnsureClipMaskWindow(HWND owner) {
  if (!owner || !IsWindow(owner) || !EnsureClipMaskWindowClass()) return false;
  if (g_clip_mask_window &&
      (!IsWindow(g_clip_mask_window) || g_clip_mask_owner != owner)) {
    if (IsWindow(g_clip_mask_window)) DestroyWindow(g_clip_mask_window);
    g_clip_mask_window = nullptr;
    g_clip_mask_owner = nullptr;
    InvalidateClipMaskCache();
  }
  if (g_clip_mask_window) return true;
  g_clip_mask_window = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
      kClipMaskClass, L"", WS_CHILD, 0, 0, 1, 1, owner, nullptr,
      GetModuleHandleW(nullptr), nullptr);
  if (!g_clip_mask_window) return false;
  g_clip_mask_owner = owner;
  InvalidateClipMaskCache();
  return true;
}

void HideClipMask() {
  g_clip_visible = false;
  if (IsWindow(g_clip_mask_window) && g_clip_drawn_visible) {
    ShowWindow(g_clip_mask_window, SW_HIDE);
  }
  g_clip_drawn_visible = false;
}

UINT WindowDpi(HWND window) {
  using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
  static GetDpiForWindowFn get_dpi = []() -> GetDpiForWindowFn {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    return user32 ? reinterpret_cast<GetDpiForWindowFn>(
                        GetProcAddress(user32, "GetDpiForWindow"))
                  : nullptr;
  }();
  const UINT dpi = get_dpi && window ? get_dpi(window) : 96;
  return dpi ? dpi : 96;
}

void UpdateClipMask() {
  if (!g_clip_visible || !IsWindow(g_clip_mask_owner) ||
      !IsWindow(g_clip_mask_view) || !EnsureClipMaskWindow(g_clip_mask_owner)) {
    HideClipMask();
    return;
  }
  RECT client{};
  if (!GetClientRect(g_clip_mask_owner, &client)) {
    HideClipMask();
    return;
  }

  const UINT dpi = WindowDpi(g_clip_mask_owner);
  const double scale = static_cast<double>(dpi) / 96.0;
  const int width = std::max(1, static_cast<int>(std::lround(g_clip_w * scale)));
  const int height = std::max(1, static_cast<int>(std::lround(g_clip_h * scale)));
  const int left = static_cast<int>(std::lround(g_clip_x * scale));
  const int top = static_cast<int>(std::lround(g_clip_y * scale));
  double radii[4] = {};
  bool any_radius = false;
  for (int i = 0; i < 4; ++i) {
    radii[i] = std::clamp(g_clip_radii[i] * scale, 0.0,
                          static_cast<double>(std::min(width, height)) / 2.0);
    any_radius = any_radius || radii[i] > 0.5;
  }
  if (!any_radius) {
    HideClipMask();
    return;
  }

  bool same_content = g_clip_drawn_valid &&
      g_clip_drawn_owner == g_clip_mask_owner &&
      g_clip_drawn_w == g_clip_w && g_clip_drawn_h == g_clip_h &&
      g_clip_drawn_dpi == dpi && g_clip_drawn_color == g_clip_color;
  for (int i = 0; same_content && i < 4; ++i) {
    same_content = g_clip_drawn_radii[i] == g_clip_radii[i];
  }
  if (same_content) {
    const bool position_changed =
        g_clip_drawn_x != g_clip_x || g_clip_drawn_y != g_clip_y;
    if (!g_clip_drawn_visible || position_changed) {
      const UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                         (g_clip_drawn_visible ? 0 : SWP_SHOWWINDOW);
      if (!SetWindowPos(g_clip_mask_window, HWND_TOP, left, top, width, height,
                        flags)) {
        HideClipMask();
        return;
      }
      g_clip_drawn_visible = true;
      g_clip_drawn_x = g_clip_x;
      g_clip_drawn_y = g_clip_y;
    }
    return;
  }

  HDC screen_dc = GetDC(nullptr);
  if (!screen_dc) { HideClipMask(); return; }
  HDC memory_dc = CreateCompatibleDC(screen_dc);
  if (!memory_dc) { ReleaseDC(nullptr, screen_dc); HideClipMask(); return; }
  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = width;
  bitmap_info.bmiHeader.biHeight = -height;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(screen_dc, &bitmap_info, DIB_RGB_COLORS,
                                    &bits, nullptr, 0);
  if (!bitmap || !bits) {
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);
    HideClipMask();
    return;
  }
  HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);
  const std::uint8_t red = static_cast<std::uint8_t>((g_clip_color >> 16) & 0xff);
  const std::uint8_t green = static_cast<std::uint8_t>((g_clip_color >> 8) & 0xff);
  const std::uint8_t blue = static_cast<std::uint8_t>(g_clip_color & 0xff);
  auto* pixels = static_cast<std::uint32_t*>(bits);
  std::memset(bits, 0, static_cast<size_t>(width) * height * sizeof(*pixels));
  constexpr int kSamples = 4;
  constexpr int kSampleCount = kSamples * kSamples;
  auto paint_corner = [&](int corner, double radius) {
    if (radius <= 0.5) return;
    const int extent = std::min(static_cast<int>(std::ceil(radius)),
                                std::min(width, height));
    const bool right = corner == 1 || corner == 2;
    const bool bottom = corner == 2 || corner == 3;
    const int x0 = right ? width - extent : 0;
    const int y0 = bottom ? height - extent : 0;
    const double cx = right ? width - radius : radius;
    const double cy = bottom ? height - radius : radius;
    for (int y = y0; y < y0 + extent; ++y) {
      for (int x = x0; x < x0 + extent; ++x) {
        int outside = 0;
        for (int sy = 0; sy < kSamples; ++sy) {
          for (int sx = 0; sx < kSamples; ++sx) {
            const double px = x + (sx + 0.5) / kSamples;
            const double py = y + (sy + 0.5) / kSamples;
            const double dx = px - cx, dy = py - cy;
            if (dx * dx + dy * dy > radius * radius) ++outside;
          }
        }
        if (!outside) continue;
        const auto alpha = static_cast<std::uint8_t>(
            (outside * 255 + kSampleCount / 2) / kSampleCount);
        const size_t index = static_cast<size_t>(y) * width + x;
        if (alpha <= static_cast<std::uint8_t>(pixels[index] >> 24)) continue;
        const auto premul_red = static_cast<std::uint8_t>((red * alpha + 127) / 255);
        const auto premul_green = static_cast<std::uint8_t>((green * alpha + 127) / 255);
        const auto premul_blue = static_cast<std::uint8_t>((blue * alpha + 127) / 255);
        pixels[index] = (static_cast<std::uint32_t>(alpha) << 24) |
                        (static_cast<std::uint32_t>(premul_red) << 16) |
                        (static_cast<std::uint32_t>(premul_green) << 8) |
                        premul_blue;
      }
    }
  };
  for (int i = 0; i < 4; ++i) paint_corner(i, radii[i]);

  const bool was_visible = g_clip_drawn_visible;
  if (!SetWindowPos(g_clip_mask_window, HWND_TOP, left, top, width, height,
                    SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);
    HideClipMask();
    InvalidateClipMaskCache();
    return;
  }
  POINT source{0, 0};
  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  const BOOL updated = UpdateLayeredWindow(
      g_clip_mask_window, screen_dc, nullptr, nullptr, memory_dc, &source,
      0, &blend, ULW_ALPHA);
  SelectObject(memory_dc, old_bitmap);
  DeleteObject(bitmap);
  DeleteDC(memory_dc);
  ReleaseDC(nullptr, screen_dc);
  if (!updated) {
    HideClipMask();
    InvalidateClipMaskCache();
    return;
  }
  if (!was_visible &&
      !SetWindowPos(g_clip_mask_window, HWND_TOP, left, top, width, height,
                    SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW)) {
    HideClipMask();
    InvalidateClipMaskCache();
    return;
  }

  g_clip_drawn_valid = true;
  g_clip_drawn_visible = true;
  g_clip_drawn_owner = g_clip_mask_owner;
  g_clip_drawn_x = g_clip_x;
  g_clip_drawn_y = g_clip_y;
  g_clip_drawn_w = g_clip_w;
  g_clip_drawn_h = g_clip_h;
  g_clip_drawn_dpi = dpi;
  g_clip_drawn_color = g_clip_color;
  for (int i = 0; i < 4; ++i) g_clip_drawn_radii[i] = g_clip_radii[i];
}

bool HashChunks(const std::vector<std::pair<const char*, size_t>>& chunks,
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
    size_t offset = 0;
    while (offset < chunk.second) {
      const ULONG count = static_cast<ULONG>(std::min<size_t>(
          chunk.second - offset, static_cast<size_t>(ULONG_MAX)));
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

bool EcdsaDerToRaw(const std::string& der, std::array<UCHAR, 64>* raw) {
  if (!raw || der.size() < 8 || der.size() > 80 ||
      static_cast<UCHAR>(der[0]) != 0x30) {
    return false;
  }
  size_t offset = 1;
  const auto read_length = [&der](size_t* cursor, size_t* length) -> bool {
    if (!cursor || !length || *cursor >= der.size()) return false;
    const UCHAR first = static_cast<UCHAR>(der[(*cursor)++]);
    if ((first & 0x80) == 0) {
      *length = first;
      return true;
    }
    const size_t count = first & 0x7f;
    if (count == 0 || count > 2 || count > der.size() - *cursor ||
        static_cast<UCHAR>(der[*cursor]) == 0) {
      return false;
    }
    size_t value = 0;
    for (size_t i = 0; i < count; ++i)
      value = (value << 8) | static_cast<UCHAR>(der[(*cursor)++]);
    if (value < 128) return false;
    *length = value;
    return true;
  };
  size_t sequence_size = 0;
  if (!read_length(&offset, &sequence_size) || sequence_size != der.size() - offset)
    return false;
  raw->fill(0);
  for (size_t component = 0; component < 2; ++component) {
    if (offset >= der.size() || static_cast<UCHAR>(der[offset++]) != 0x02)
      return false;
    size_t integer_size = 0;
    if (!read_length(&offset, &integer_size) || integer_size == 0 ||
        integer_size > der.size() - offset) {
      return false;
    }
    const UCHAR* integer = reinterpret_cast<const UCHAR*>(der.data() + offset);
    if ((integer[0] & 0x80) != 0) return false;  // ECDSA integers are positive.
    if (integer_size > 1 && integer[0] == 0) {
      if ((integer[1] & 0x80) == 0) return false;  // Reject non-canonical DER.
      ++integer;
      --integer_size;
    }
    if (integer_size > 32) return false;
    const size_t destination = component * 32 + (32 - integer_size);
    std::copy(integer, integer + integer_size, raw->begin() + destination);
    offset += static_cast<size_t>(integer -
                                  reinterpret_cast<const UCHAR*>(
                                      der.data() + offset)) +
              integer_size;
  }
  return offset == der.size();
}

}  // namespace

std::string UiResourceDir() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
  std::wstring path(buf, n);
  size_t slash = path.find_last_of(L"\\/");
  if (slash != std::wstring::npos) path.resize(slash);
  return Utf8(path) + "\\ui";
}

std::string FiltersDir() {
  std::string d = UiResourceDir();
  const size_t p = d.find_last_of("\\/");
  return (p == std::string::npos ? d : d.substr(0, p)) + "\\filters";
}

std::string UserDataDir() {
  std::string base = KnownFolder(FOLDERID_LocalAppData);
  if (base.empty()) base = ".";
  return base + "\\SHELTER";
}

bool RandomBytes(size_t size, std::string* out) {
  if (!out || size > static_cast<size_t>(ULONG_MAX)) return false;
  out->assign(size, '\0');
  if (size == 0) return true;
  if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(out->data()),
                      static_cast<ULONG>(size),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0)
    return true;
  out->clear();
  return false;
}

bool Sha256(const std::string& bytes, std::string* digest) {
  return HashChunks({{bytes.data(), bytes.size()}}, digest);
}

bool VerifyCrx3Signature(const std::string& public_key_der, bool ecdsa,
                         const std::string& signed_header_data,
                         const char* zip_data, size_t zip_size,
                         const std::string& signature) {
  if (public_key_der.empty() || public_key_der.size() > 8192 ||
      signed_header_data.size() > 1024u * 1024u || !zip_data || zip_size == 0 ||
      zip_size > 64u * 1024u * 1024u || signature.empty() ||
      signature.size() > static_cast<size_t>(ULONG_MAX) ||
      signed_header_data.size() > UINT32_MAX) {
    return false;
  }
  constexpr char kContext[] = "CRX3 SignedData";
  const std::uint32_t header_size =
      static_cast<std::uint32_t>(signed_header_data.size());
  const char header_size_le[4] = {
      static_cast<char>(header_size), static_cast<char>(header_size >> 8),
      static_cast<char>(header_size >> 16),
      static_cast<char>(header_size >> 24)};
  std::string digest;
  const std::vector<std::pair<const char*, size_t>> chunks = {
      {kContext, sizeof(kContext) - 1},
      {header_size_le, sizeof(header_size_le)},
      {signed_header_data.data(), signed_header_data.size()},
      {zip_data, zip_size}};
  if (!HashChunks(chunks, &digest) || digest.size() != 32) return false;

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
        parameters.cbData != sizeof(kPrime256v1Oid) ||
        !parameters.pbData ||
        memcmp(parameters.pbData, kPrime256v1Oid, sizeof(kPrime256v1Oid)) != 0) {
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
      ((!ecdsa && key_bits >= 2048 && key_bits <= 8192) ||
       (ecdsa && key_bits == 256))) {
    if (ecdsa) {
      std::array<UCHAR, 64> raw_signature{};
      if (EcdsaDerToRaw(signature, &raw_signature)) {
        verified = BCryptVerifySignature(
                       key, nullptr,
                       reinterpret_cast<PUCHAR>(digest.data()),
                       static_cast<ULONG>(digest.size()), raw_signature.data(),
                       static_cast<ULONG>(raw_signature.size()), 0) == 0;
      }
    } else if (signature.size() == (key_bits + 7u) / 8u) {
      BCRYPT_PKCS1_PADDING_INFO padding = {BCRYPT_SHA256_ALGORITHM};
      verified = BCryptVerifySignature(
                     key, &padding,
                     reinterpret_cast<PUCHAR>(digest.data()),
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

std::string DownloadsDir() {
  std::string d = KnownFolder(FOLDERID_Downloads);
  if (!d.empty()) return d;
  // Keep downloads outside the disposable browser profile even when the
  // Downloads known folder is unavailable.
  std::string profile = KnownFolder(FOLDERID_Profile);
  if (!profile.empty()) return profile + "\\Downloads";
  std::string local = KnownFolder(FOLDERID_LocalAppData);
  if (!local.empty()) return local + "\\Downloads";  // sibling of the SHELTER profile
  wchar_t temp[32768] = {};
  const DWORD temp_capacity = static_cast<DWORD>(sizeof(temp) / sizeof(temp[0]));
  const DWORD temp_len = GetTempPathW(temp_capacity, temp);
  if (temp_len > 0 && temp_len < temp_capacity)
    return Utf8(std::wstring(temp, temp_len)) + "SHELTER Downloads";
  return UiResourceDir() + "\\Downloads";
}

bool DeleteUserData() {
  namespace fs = std::filesystem;
  const std::string local_app_data = KnownFolder(FOLDERID_LocalAppData);
  if (local_app_data.empty()) return false;
  const fs::path expected = fs::u8path(local_app_data) / "SHELTER";
  const fs::path target = fs::u8path(UserDataDir());
  if (target.filename().string() != "SHELTER" ||
      target.lexically_normal() != expected.lexically_normal()) {
    return false;
  }
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(target, ec);
  if (ec == std::errc::no_such_file_or_directory) return true;
  if (ec) return false;
  if (status.type() == fs::file_type::not_found) return true;
  return security::RemoveTreeWithoutFollowingLinks(expected.parent_path(), target);
}

void ShowUserDataDeletionFailure() {
  MessageBoxW(nullptr,
              L"Не удалось полностью удалить профиль SHELTER. Некоторые файлы или ключ приложения могли остаться. Проверьте права доступа и повторите удаление.",
              L"SHELTER", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
}

void ShowInFolder(const std::string& path) {
  const std::wstring wide_path = Wide(path);
  if (wide_path.empty()) return;

  PIDLIST_ABSOLUTE item = nullptr;
  SFGAOF attributes = SFGAO_FILESYSTEM | SFGAO_FOLDER;
  const HRESULT parsed = SHParseDisplayName(
      wide_path.c_str(), nullptr, &item, attributes, &attributes);
  if (FAILED(parsed) || !item || !(attributes & SFGAO_FILESYSTEM) ||
      (attributes & SFGAO_FOLDER)) {
    if (item) CoTaskMemFree(item);
    return;
  }

  PIDLIST_ABSOLUTE folder = ILClone(item);
  PCUITEMID_CHILD child = ILFindLastID(item);
  if (!folder || !child || !ILRemoveLastID(folder)) {
    if (folder) CoTaskMemFree(folder);
    CoTaskMemFree(item);
    return;
  }
  PCUITEMID_CHILD selected[] = {child};
  SHOpenFolderAndSelectItems(folder, 1, selected, 0);
  CoTaskMemFree(folder);
  CoTaskMemFree(item);
}

std::string ClipboardRead() {
  std::string out;
  if (!OpenClipboard(nullptr)) return out;
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  if (h) {
    const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h));
    if (p) {
      out = Utf8(p);
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return out;
}

bool ClipboardWrite(const std::string& text) {
  std::wstring w = Wide(text);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
  if (!mem) return false;
  void* dst = GlobalLock(mem);
  if (!dst) {
    GlobalFree(mem);
    return false;
  }
  memcpy(dst, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
  GlobalUnlock(mem);
  if (!OpenClipboard(nullptr)) {
    GlobalFree(mem);
    return false;
  }
  EmptyClipboard();
  bool ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
  if (!ok) GlobalFree(mem);
  CloseClipboard();
  return ok;
}

void SetColorScheme(bool /*dark*/) {}

std::string DebugHitTest(double, double) { return std::string(); }

static int g_clip_level = -1;

void SetClipLevel(int level) { g_clip_level = level; }

std::string DumpWindowChain(void* handle) {
  HWND h = static_cast<HWND>(handle);
  std::string out;
  HWND root = h ? GetAncestor(h, GA_ROOT) : nullptr;
  for (HWND c = h; c; c = GetParent(c)) {
    wchar_t cls[128] = {0};
    GetClassNameW(c, cls, 127);
    RECT r{};
    GetWindowRect(c, &r);
    char buf[512];
    char cls8[256] = {0};
    WideCharToMultiByte(CP_UTF8, 0, cls, -1, cls8, 255, nullptr, nullptr);
    RECT rb{};
    int rt = GetWindowRgnBox(c, &rb);
    snprintf(buf, sizeof(buf), "  hwnd=%p cls=%s rect=%ld,%ld,%ld,%ld style=%08lx ex=%08lx rgn=%d\n", (void*)c, cls8,
             r.left, r.top, r.right - r.left, r.bottom - r.top,
             static_cast<unsigned long>(GetWindowLongPtrW(c, GWL_STYLE)),
             static_cast<unsigned long>(GetWindowLongPtrW(c, GWL_EXSTYLE)), rt);
    out += buf;
    if (c == root) break;
  }
  if (root) {
    out += " root children:\n";
    for (HWND c = GetWindow(root, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
      wchar_t cls[128] = {0};
      GetClassNameW(c, cls, 127);
      char cls8[256] = {0};
      WideCharToMultiByte(CP_UTF8, 0, cls, -1, cls8, 255, nullptr, nullptr);
      RECT r{};
      GetWindowRect(c, &r);
      char buf[512];
      snprintf(buf, sizeof(buf), "  child hwnd=%p cls=%s rect=%ld,%ld,%ld,%ld vis=%d\n", (void*)c, cls8, r.left,
               r.top, r.right - r.left, r.bottom - r.top, IsWindowVisible(c) ? 1 : 0);
      out += buf;
    }
  }
  return out;
}

void InstallInputFixes() {}

void WatchMainWindow(void*, void (*)(void*), void (*)(void*), void*) {}
void UnwatchMainWindow(void*) {}

void ApplyViewClip(void* handle, const double radii[4],
                   const std::vector<std::array<int, 5>>& /*holes*/,
                   int view_x, int view_y, int view_w, int view_h,
                   bool visible, std::uint32_t mask_color) {
  HWND view = handle ? static_cast<HWND>(handle) : nullptr;
  if (!visible) {
    if (!view || view == g_clip_mask_view) HideClipMask();
    return;
  }
  if (!view) {
    HideClipMask();
    return;
  }
  if (view_w <= 0 || view_h <= 0) {
    if (view == g_clip_mask_view) HideClipMask();
    return;
  }
  if (!IsWindow(view)) {
    if (view == g_clip_mask_view) HideClipMask();
    return;
  }
  HWND owner = GetAncestor(view, GA_ROOT);
  if (!owner) {
    if (view == g_clip_mask_view) HideClipMask();
    return;
  }
  bool rounded = false;
  for (int i = 0; i < 4; ++i) rounded = rounded || radii[i] > 0.5;
  if (!rounded) {
    g_clip_mask_view = view;
    HideClipMask();
    return;
  }
  if (!EnsureClipMaskWindow(owner)) {
    if (view == g_clip_mask_view) HideClipMask();
    return;
  }
  g_clip_mask_owner = owner;
  g_clip_mask_view = view;
  g_clip_x = view_x;
  g_clip_y = view_y;
  g_clip_w = view_w;
  g_clip_h = view_h;
  g_clip_color = mask_color & 0x00ffffffu;
  for (int i = 0; i < 4; ++i) g_clip_radii[i] = std::max(0.0, radii[i]);
  g_clip_visible = true;
  UpdateClipMask();
}

}  // namespace platform
}  // namespace shelter
