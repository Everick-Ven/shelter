#include "app/cef/cef_app.h"
#include "app/common/logging.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

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
