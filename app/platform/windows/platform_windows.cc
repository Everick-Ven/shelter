#include "app/platform/platform.h"
#if defined(_WIN32)
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
namespace shelter {
namespace {
std::wstring Utf8ToWide(std::string_view text) {
  if (text.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                              static_cast<int>(text.size()), nullptr, 0);
  if (n <= 0) {
    n = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                            static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) return {};
  }
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), n);
  return out;
}
std::string WideToUtf8(const wchar_t* s, size_t len) {
  if (!s || len == 0) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(len), nullptr, 0,
                              nullptr, nullptr);
  if (n <= 0) return {};
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(len), out.data(), n,
                      nullptr, nullptr);
  return out;
}
}  // namespace
std::optional<std::string> PlatformClipboardRead() {
  if (!OpenClipboard(nullptr)) return std::nullopt;
  std::optional<std::string> out;
  HANDLE data = GetClipboardData(CF_UNICODETEXT);
  if (data) {
    const auto* text = static_cast<const wchar_t*>(GlobalLock(data));
    if (text) {
      out = WideToUtf8(text, wcslen(text));
      GlobalUnlock(data);
    }
  }
  CloseClipboard();
  return out;
}
bool PlatformClipboardWrite(std::string_view text) {
  std::wstring wide = Utf8ToWide(text);
  if (!OpenClipboard(nullptr)) return false;
  EmptyClipboard();
  bool ok = false;
  const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (mem) {
    void* dst = GlobalLock(mem);
    if (dst) {
      std::memcpy(dst, wide.c_str(), bytes);
      GlobalUnlock(mem);
      if (SetClipboardData(CF_UNICODETEXT, mem)) ok = true;
    }
    if (!ok) GlobalFree(mem);
  }
  CloseClipboard();
  return ok;
}
std::string PlatformDefaultDownloadsDir() {
  const char* home = std::getenv("USERPROFILE");
  std::filesystem::path dir =
      std::filesystem::path(home && *home ? home : ".") / "Downloads";
  return dir.string();
}
}  // namespace shelter
#endif
