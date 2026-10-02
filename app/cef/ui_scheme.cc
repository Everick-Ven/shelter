#include "app/cef/ui_scheme.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "include/cef_parser.h"
#include "include/cef_resource_handler.h"
#include "include/cef_stream.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace shelter {
namespace {
// Resolves the directory that contains index.html for the shelter://ui
// scheme. Works from the repository (dev), from the portable Windows package
// (<exe>/Resources/ui) and from the macOS bundle (Contents/Resources/ui).
std::filesystem::path ExecutablePath() {
#if defined(_WIN32)
  wchar_t buffer[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    return std::filesystem::path(std::wstring(buffer, n));
  }
  return {};
#elif defined(__APPLE__)
  char buffer[4096];
  uint32_t size = sizeof(buffer);
  if (_NSGetExecutablePath(buffer, &size) == 0) {
    return std::filesystem::path(buffer);
  }
  return {};
#else
  char buffer[4096];
  const ssize_t n = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (n > 0) {
    buffer[n] = '\0';
    return std::filesystem::path(buffer);
  }
  return {};
#endif
}

const std::filesystem::path& UiDir() {
  static const std::filesystem::path dir = [] {
    std::vector<std::filesystem::path> candidates;
    if (const char* env = std::getenv("SHELTER_UI_DIR"); env && *env) {
      candidates.emplace_back(env);
    }
    const std::filesystem::path exe = ExecutablePath();
    if (!exe.empty()) {
      const std::filesystem::path exe_dir = exe.parent_path();
      candidates.push_back(exe_dir / "Resources" / "ui");           // Windows pkg
      candidates.push_back(exe_dir / ".." / "Resources" / "ui");    // macOS bundle
    }
    const std::filesystem::path cwd = std::filesystem::current_path();
    candidates.push_back(cwd / "resources" / "ui");                 // repo dev
    candidates.push_back(cwd / "Resources" / "ui");
    for (const auto& c : candidates) {
      std::error_code ec;
      std::filesystem::path p = std::filesystem::weakly_canonical(c, ec);
      if (ec) p = c;
      if (std::filesystem::exists(p / "index.html", ec)) return p;
    }
    return candidates.front();
  }();
  return dir;
}

std::string MimeTypeFor(const std::string& ext) {
  if (ext == "html" || ext == "htm") return "text/html";
  if (ext == "js" || ext == "mjs") return "application/javascript";
  if (ext == "css") return "text/css";
  if (ext == "json" || ext == "map") return "application/json";
  if (ext == "svg") return "image/svg+xml";
  if (ext == "png") return "image/png";
  if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
  if (ext == "gif") return "image/gif";
  if (ext == "webp") return "image/webp";
  if (ext == "ico") return "image/x-icon";
  if (ext == "txt") return "text/plain";
  if (ext == "woff") return "font/woff";
  if (ext == "woff2") return "font/woff2";
  return "application/octet-stream";
}

bool IsSafePath(const std::string& path) {
  if (path.empty()) return false;
  for (const unsigned char c : path) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                    c == '-' || c == '/';
    if (!ok) return false;
  }
  return path.find("..") == std::string::npos;
}

class Handler final : public CefResourceHandler {
 public:
  Handler(std::string data, std::string mime)
      : data_(std::move(data)), mime_(std::move(mime)) {}
  bool ProcessRequest(CefRefPtr<CefRequest> r, CefRefPtr<CefCallback> cb) override {
    cb->Continue();
    return true;
  }
  void GetResponseHeaders(CefRefPtr<CefResponse> r, int64_t& size,
                          CefString& redirect) override {
    r->SetStatus(200);
    r->SetMimeType(mime_);
    r->SetHeaderMap({{"Cache-Control", "no-store"}});
    size = static_cast<int64_t>(data_.size());
  }
  bool ReadResponse(void* buffer, int size, int& out,
                    CefRefPtr<CefCallback> cb) override {
    out = 0;
    if (off_ >= data_.size()) return false;
    const size_t chunk = std::min<size_t>(static_cast<size_t>(size),
                                          data_.size() - off_);
    memcpy(buffer, data_.data() + off_, chunk);
    off_ += chunk;
    out = static_cast<int>(chunk);
    return true;
  }
  bool Skip(int64_t bytes, int64_t& bytes_skipped,
            CefRefPtr<CefResourceSkipCallback> cb) override {
    bytes_skipped = 0;
    return false;
  }
  void Cancel() override {}

 private:
  std::string data_;
  std::string mime_;
  size_t off_ = 0;
  IMPLEMENT_REFCOUNTING(Handler);
};

// Native-frame presentation glue: the OS title bar already provides
// minimize/maximize/close and window dragging, so hide the UI's duplicate
// controls instead of showing two sets of buttons.
const char* kFrameStyleInjection =
    "<style id=\"shelter-native-frame\">"
    "/* injected: native window frame is used, hide UI duplicates */"
    "#winCtl,#winLights{display:none!important}"
    "</style>";

class Factory final : public CefSchemeHandlerFactory {
 public:
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme,
                                       CefRefPtr<CefRequest> request) override {
    std::string path = request->GetURL().ToString();
    const size_t query = path.find_first_of("?#");
    if (query != std::string::npos) path = path.substr(0, query);
    // Strip "shelter://ui" prefix (and a following slash).
    const char* prefix = "shelter://ui";
    if (path.compare(0, std::strlen(prefix), prefix) == 0) {
      path = path.substr(std::strlen(prefix));
    }
    if (!path.empty() && path[0] == '/') path = path.substr(1);
    if (path.empty() || path.back() == '/') path += "index.html";
    if (!IsSafePath(path)) return nullptr;

    const std::filesystem::path file = UiDir() / path;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return nullptr;

    std::ifstream stream(file, std::ios::binary);
    if (!stream) return nullptr;
    std::string data((std::istreambuf_iterator<char>(stream)),
                     std::istreambuf_iterator<char>());

    std::string ext;
    const size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(tolower(c)); });

    if (ext == "html" || ext == "htm") {
      const size_t head = data.find("</head>");
      if (head != std::string::npos &&
          data.find("shelter-native-frame") == std::string::npos) {
        data.insert(head, kFrameStyleInjection);
      }
    }
    return new Handler(std::move(data), MimeTypeFor(ext));
  }

 private:
  IMPLEMENT_REFCOUNTING(Factory);
};
}  // namespace

void RegisterUiScheme() {
  CefRegisterSchemeHandlerFactory("shelter", "ui", new Factory);
}
}  // namespace shelter
