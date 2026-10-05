#include "app/cef/ui_scheme.h"

#include "app/common/logging.h"
#include "include/cef_parser.h"
#include "include/cef_resource_handler.h"
#include "include/cef_stream.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace shelter {
namespace {
namespace fs = std::filesystem;

std::string PathToUtf8(const fs::path& path) {
  const auto value = path.u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

class Handler final : public CefResourceHandler {
 public:
  Handler(std::string data, std::string mime_type, int status)
      : data_(std::move(data)), mime_type_(std::move(mime_type)), status_(status) {}

  bool ProcessRequest(CefRefPtr<CefRequest>, CefRefPtr<CefCallback> callback) override {
    callback->Continue();
    return true;
  }

  void GetResponseHeaders(CefRefPtr<CefResponse> response,
                          int64_t& response_length,
                          CefString&) override {
    response->SetStatus(status_);
    response->SetStatusText(status_ == 200 ? "OK" : "Not Found");
    response->SetMimeType(mime_type_);
    response_length = static_cast<int64_t>(data_.size());
  }

  bool ReadResponse(void* data_out,
                    int bytes_to_read,
                    int& bytes_read,
                    CefRefPtr<CefCallback>) override {
    bytes_read = 0;
    if (bytes_to_read <= 0 || offset_ >= data_.size()) return false;

    const size_t count = std::min(static_cast<size_t>(bytes_to_read), data_.size() - offset_);
    std::memcpy(data_out, data_.data() + offset_, count);
    offset_ += count;
    bytes_read = static_cast<int>(count);
    return true;
  }

  bool Skip(int64_t, int64_t& bytes_skipped, CefRefPtr<CefResourceSkipCallback>) override {
    bytes_skipped = 0;
    return false;
  }

  void Cancel() override {}

 private:
  std::string data_;
  std::string mime_type_;
  int status_;
  size_t offset_ = 0;

  IMPLEMENT_REFCOUNTING(Handler);
};

CefRefPtr<CefResourceHandler> NotFound() {
  return new Handler("Not Found", "text/plain", 404);
}

fs::path ExecutablePath() {
#if defined(_WIN32)
  constexpr size_t kMaxPathLength = 32768;
  std::vector<wchar_t> buffer(512);
  while (buffer.size() <= kMaxPathLength) {
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) return {};
    if (static_cast<size_t>(length) < buffer.size()) {
      return fs::path(std::wstring(buffer.data(), static_cast<size_t>(length)));
    }
    if (buffer.size() == kMaxPathLength) return {};
    buffer.resize(std::min(buffer.size() * 2, kMaxPathLength));
  }
#elif defined(__APPLE__)
  std::vector<char> buffer(1024);
  for (int attempt = 0; attempt < 8; ++attempt) {
    uint32_t length = static_cast<uint32_t>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &length) == 0) return fs::path(buffer.data());
    const size_t next_size = length > buffer.size() ? length : buffer.size() * 2;
    buffer.resize(next_size);
  }
#endif
  return {};
}

fs::path ResolveUiDirectory() {
  std::vector<fs::path> candidates;
  const fs::path executable = ExecutablePath();
  if (!executable.empty()) {
    const fs::path executable_dir = executable.parent_path();
    candidates.push_back(executable_dir / "Resources" / "ui");
#if defined(__APPLE__)
    // A bundled macOS executable lives in Contents/MacOS; UI assets live in Contents/Resources.
    candidates.push_back(executable_dir.parent_path() / "Resources" / "ui");
#endif
    candidates.push_back(executable_dir / "resources" / "ui");
  }

  // Keep running directly from a source checkout convenient; packaged builds use the
  // executable-relative locations above and do not depend on the process working directory.
  std::error_code error;
  const fs::path working_directory = fs::current_path(error);
  if (!error) {
    candidates.push_back(working_directory / "Resources" / "ui");
    candidates.push_back(working_directory / "resources" / "ui");
  }

  for (const fs::path& candidate : candidates) {
    error.clear();
    if (fs::is_regular_file(candidate / "index.html", error)) return candidate;
  }

  return candidates.empty() ? fs::path("resources/ui") : candidates.front();
}

std::string Lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool ParseUiResourcePath(CefRefPtr<CefRequest> request, fs::path& relative_path) {
  if (!request) return false;

  CefURLParts parts;
  if (!CefParseURL(request->GetURL(), parts)) return false;

  const std::string scheme = Lowercase(CefString(&parts.scheme).ToString());
  const std::string host = Lowercase(CefString(&parts.host).ToString());
  if (scheme != "shelter" || host != "ui") return false;

  std::string path = CefString(&parts.path).ToString();
  if (path.empty() || path == "/") path = "/index.html";
  if (path.front() == '/') path.erase(path.begin());
  if (path.empty() || path.find('\\') != std::string::npos) return false;

  relative_path = fs::path(path);
  if (relative_path.is_absolute() || relative_path.has_root_name() ||
      relative_path.has_root_directory()) {
    return false;
  }
  for (const fs::path& component : relative_path) {
    if (component == "." || component == "..") return false;
  }
  return true;
}

std::string MimeTypeFor(const fs::path& path) {
  const std::string extension = Lowercase(path.extension().string());
  if (extension == ".html" || extension == ".htm") return "text/html";
  if (extension == ".js" || extension == ".mjs") return "application/javascript";
  if (extension == ".css") return "text/css";
  if (extension == ".json") return "application/json";
  if (extension == ".svg") return "image/svg+xml";
  if (extension == ".png") return "image/png";
  if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
  if (extension == ".webp") return "image/webp";
  if (extension == ".woff") return "font/woff";
  if (extension == ".woff2") return "font/woff2";
  if (extension == ".txt") return "text/plain";
  return "application/octet-stream";
}

class Factory final : public CefSchemeHandlerFactory {
 public:
  explicit Factory(fs::path ui_directory) : ui_directory_(std::move(ui_directory)) {}

  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>,
                                       CefRefPtr<CefFrame>,
                                       const CefString&,
                                       CefRefPtr<CefRequest> request) override {
    fs::path relative_path;
    if (!ParseUiResourcePath(request, relative_path)) return NotFound();

    const fs::path resource_path = (ui_directory_ / relative_path).lexically_normal();
    std::error_code error;
    if (!fs::is_regular_file(resource_path, error)) {
      Log(LogLevel::Error,
          "SHELTER UI resource not found: " + PathToUtf8(resource_path));
      return NotFound();
    }

    std::ifstream file(resource_path, std::ios::binary);
    if (!file) {
      Log(LogLevel::Error,
          "Failed to open SHELTER UI resource: " + PathToUtf8(resource_path));
      return NotFound();
    }

    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return new Handler(std::move(data), MimeTypeFor(resource_path), 200);
  }

 private:
  fs::path ui_directory_;

  IMPLEMENT_REFCOUNTING(Factory);
};
}  // namespace

void RegisterUiScheme() {
  const fs::path ui_directory = ResolveUiDirectory();
  std::error_code error;
  const fs::path index_path = ui_directory / "index.html";
  if (!fs::is_regular_file(index_path, error)) {
    Log(LogLevel::Error,
        "SHELTER UI index.html was not found: " + PathToUtf8(index_path));
  } else {
    Log(LogLevel::Info,
        "SHELTER UI resources resolved: " + PathToUtf8(ui_directory));
  }

  if (!CefRegisterSchemeHandlerFactory("shelter", "ui", new Factory(ui_directory))) {
    Log(LogLevel::Error, "Failed to register SHELTER UI scheme handler");
  }
}
}  // namespace shelter
