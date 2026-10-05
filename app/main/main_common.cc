#include "app/cef/cef_app.h"
#include "app/common/logging.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#if defined(__APPLE__)
#include <cstdint>
#include <mach-o/dyld.h>
#endif
#if defined(_WIN32)
#include <objbase.h>
#include <shlobj.h>
#include <windows.h>
#endif

namespace {

std::string PathToUtf8(const std::filesystem::path& path) {
  const auto value = path.u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

#if defined(__APPLE__)
std::filesystem::path GetExecutablePath(const char* argv0) {
  namespace fs = std::filesystem;
  uint32_t path_size = 0;
  _NSGetExecutablePath(nullptr, &path_size);
  if (path_size > 0) {
    std::string path_buffer(path_size, '\0');
    if (_NSGetExecutablePath(path_buffer.data(), &path_size) == 0) {
      const fs::path path(path_buffer.c_str());
      std::error_code error;
      const fs::path canonical_path = fs::weakly_canonical(path, error);
      return error ? path : canonical_path;
    }
  }

  std::error_code error;
  const fs::path path = fs::absolute(fs::path(argv0), error);
  return error ? fs::path(argv0) : path;
}
#endif

std::filesystem::path GetProfilePath() {
  namespace fs = std::filesystem;
  fs::path base;
#if defined(_WIN32)
  PWSTR local_app_data = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
                                     nullptr, &local_app_data)) && local_app_data) {
    base = fs::path(local_app_data) / L"SHELTER" / L"CEF";
    CoTaskMemFree(local_app_data);
  }
#else
  if (const char* home = std::getenv("HOME")) {
    base = fs::path(home) / "Library" / "Application Support" / "SHELTER" / "CEF";
  }
#endif
  if (base.empty()) base = fs::temp_directory_path() / "SHELTER" / "CEF";
  std::error_code error;
  fs::create_directories(base, error);
  return base;
}

}  // namespace

int main(int argc, char** argv) {
  namespace fs = std::filesystem;
  const fs::path profile_path = GetProfilePath();
  const fs::path application_support_path = profile_path.parent_path();
  std::error_code log_directory_error;
  fs::create_directories(application_support_path, log_directory_error);
  const std::string application_log_path =
      PathToUtf8(application_support_path / "shelter.log");
  shelter::InitializeLogging(application_log_path.c_str());
#if defined(__APPLE__)
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInMain()) {
    shelter::Log(shelter::LogLevel::Error,
                 "Failed to load the CEF framework for the browser process");
    return 1;
  }
#endif
#if defined(_WIN32)
  CefMainArgs args(GetModuleHandle(nullptr));
#else
  CefMainArgs args(argc, argv);
#endif
  CefRefPtr<shelter::App> app(new shelter::App);
  int exit_code = CefExecuteProcess(args, app, nullptr);
  if (exit_code >= 0) return exit_code;

  CefSettings settings;
  settings.no_sandbox = true;
  CefString(&settings.cache_path) = PathToUtf8(profile_path);
  settings.persist_session_cookies = true;
#if defined(__APPLE__)
  // The helper executable lives in Contents/Frameworks inside the top-level
  // app bundle. Resolve the running executable rather than relying on cwd or
  // argv[0], which can be relative when launched outside Finder.
  namespace fs = std::filesystem;
  const fs::path executable_path = GetExecutablePath(argv[0]);
  const fs::path bundle_contents = executable_path.parent_path().parent_path();
  const fs::path helper_path =
      bundle_contents / "Frameworks" / "SHELTER Helper.app" / "Contents" /
      "MacOS" / "SHELTER Helper";
  std::error_code helper_error;
  if (!fs::is_regular_file(helper_path, helper_error) || helper_error) {
    shelter::Log(shelter::LogLevel::Error,
                 "CEF helper executable is missing: " + PathToUtf8(helper_path));
    return 1;
  }
  CefString(&settings.browser_subprocess_path) = PathToUtf8(helper_path);
#endif
  const std::string cef_log_path =
      PathToUtf8(application_support_path / "shelter-cef.log");
  CefString(&settings.log_file) = cef_log_path;
  if (!CefInitialize(args, settings, app, nullptr)) {
    shelter::Log(shelter::LogLevel::Error, "CefInitialize failed");
    return 1;
  }
  shelter::Log(shelter::LogLevel::Info,
               "CEF initialized; entering the browser message loop");
  CefRunMessageLoop();
  CefShutdown();
  return 0;
}
