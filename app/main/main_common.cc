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
    base = fs::u8path(home) / "Library" / "Application Support" / "SHELTER" / "CEF";
  }
#endif
  if (base.empty()) base = fs::temp_directory_path() / "SHELTER" / "CEF";
  std::error_code error;
  fs::create_directories(base, error);
  return base;
}

}  // namespace

int main(int argc, char** argv) {
  shelter::InitializeLogging("shelter.log");
#if defined(__APPLE__)
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInMain()) return 1;
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
  CefString(&settings.cache_path) = PathToUtf8(GetProfilePath());
  settings.persist_session_cookies = true;
#if defined(__APPLE__)
  // CEF on macOS requires a separate helper application for renderer,
  // GPU and utility subprocesses when running as an app bundle.
  std::filesystem::path executable_path(argv[0]);
  auto helper_path = executable_path.parent_path().parent_path().parent_path() /
                     "Frameworks/SHELTER Helper.app/Contents/MacOS/SHELTER Helper";
  CefString(&settings.browser_subprocess_path) = helper_path.string();
#endif
  CefString(&settings.log_file) = "shelter-cef.log";
  if (!CefInitialize(args, settings, app, nullptr)) return 1;
  CefRunMessageLoop();
  CefShutdown();
  return 0;
}
