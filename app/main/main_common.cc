#include "app/cef/cef_app.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#include "app/common/logging.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"

#if defined(_WIN32)
#include <windows.h>
#if defined(_MSC_VER)
// GUI application: no console window. Entry point stays main().
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:mainCRTStartup")
#endif
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {
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
  return {};
#endif
}

// Per-user data directory. Must be absolute and set as root_cache_path before
// CefInitialize (process singleton + persistence requirements of CEF >= 120).
std::filesystem::path UserDataDir() {
#if defined(_WIN32)
  const char* base = std::getenv("LOCALAPPDATA");
  if (!base || !*base) base = std::getenv("USERPROFILE");
  return std::filesystem::path(base && *base ? base : ".") / "SHELTER";
#elif defined(__APPLE__)
  const char* home = std::getenv("HOME");
  return std::filesystem::path(home && *home ? home : ".") /
         "Library" / "Application Support" / "SHELTER";
#else
  return std::filesystem::path(".shelter");
#endif
}
}  // namespace

int main(int argc, char** argv) {
  const std::filesystem::path data_dir = UserDataDir();
  std::error_code ec;
  std::filesystem::create_directories(data_dir, ec);

  shelter::InitializeLogging((data_dir / "shelter.log").string().c_str());

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
  const int exit_code = CefExecuteProcess(args, app, nullptr);
  if (exit_code >= 0) return exit_code;

  CefSettings settings;
  settings.no_sandbox = true;
#if defined(__APPLE__)
  // CEF on macOS requires a separate helper application for renderer, GPU and
  // utility subprocesses when running as an app bundle.
  std::filesystem::path executable_path = ExecutablePath();
  if (executable_path.empty() && argc > 0) executable_path = argv[0];
  const auto helper_path = executable_path.parent_path().parent_path().parent_path() /
                           "Frameworks/SHELTER Helper.app/Contents/MacOS/SHELTER Helper";
  CefString(&settings.browser_subprocess_path) = helper_path.string();
#endif
  // Required by CEF >= 120: absolute installation/profile root. Also enables
  // localStorage persistence for the SHELTER UI (cache_path == root).
  CefString(&settings.root_cache_path) = data_dir.string();
  CefString(&settings.cache_path) = data_dir.string();
  CefString(&settings.log_file) = (data_dir / "shelter-cef.log").string();

  if (!CefInitialize(args, settings, app, nullptr)) return 1;
  CefRunMessageLoop();
  CefShutdown();
  return 0;
}
