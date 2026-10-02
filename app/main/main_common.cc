#include "app/cef/cef_app.h"

#include <cstdlib>
#include <cstring>
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
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <unistd.h>
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
    // Resolve to an absolute path: _NSGetExecutablePath may return argv0-style
    // relative paths, and every bundle-relative path below assumes absolute.
    char resolved[4096];
    if (realpath(buffer, resolved) != nullptr) {
      return std::filesystem::path(resolved);
    }
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
  // Chromium CHECK/FATAL diagnostics go to stderr, which is discarded when
  // the app is launched from Finder. Mirror stderr into a log file so that
  // crashes report their message. Helper sub-processes inherit this
  // descriptor from the main process.
  const int err_fd =
      open((data_dir / "shelter-stderr.log").c_str(),
           O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (err_fd >= 0) {
    dup2(err_fd, STDERR_FILENO);
    if (err_fd != STDERR_FILENO) close(err_fd);
  }

  // Sub-processes (renderer/GPU/utility) are launched with --type=<...> and
  // must load the framework relative to the OUTER app bundle
  // (Contents/Frameworks/<helper>.app/Contents/MacOS -> ../../../Frameworks),
  // while the main process loads it relative to its own bundle
  // (Contents/MacOS -> ../Frameworks). Calling LoadInMain() from a helper
  // resolves to <helper>.app/Contents/Frameworks, the load fails, every
  // sub-process exits immediately, and the browser process later CHECK-fails
  // on the UI thread waiting for a renderer.
  bool is_helper = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strncmp(argv[i], "--type=", 7) == 0) {
      is_helper = true;
      break;
    }
  }
  shelter::Log(shelter::LogLevel::Info,
               std::string("main: start argv0=") +
                   (argc > 0 ? argv[0] : "<none>") +
                   " is_helper=" + (is_helper ? "1" : "0"));
  CefScopedLibraryLoader library_loader;
  const bool load_failed =
      is_helper ? !library_loader.LoadInHelper()
                : !library_loader.LoadInMain();
  if (load_failed) {
    shelter::Log(shelter::LogLevel::Error,
                 is_helper ? "CEF framework load failed (helper)"
                           : "CEF framework load failed (main)");
    return 1;
  }
  shelter::Log(shelter::LogLevel::Info, "main: framework loaded");
#endif

#if defined(_WIN32)
  CefMainArgs args(GetModuleHandle(nullptr));
#else
  CefMainArgs args(argc, argv);
#endif
  CefRefPtr<shelter::App> app(new shelter::App);
  const int exit_code = CefExecuteProcess(args, app, nullptr);
  if (exit_code >= 0) {
    shelter::Log(shelter::LogLevel::Info,
                 "main: CefExecuteProcess rc=" + std::to_string(exit_code));
    return exit_code;
  }
  shelter::Log(shelter::LogLevel::Info, "main: CefInitialize entering");

  CefSettings settings;
  settings.no_sandbox = true;
#if defined(__APPLE__)
  // CEF on macOS requires a separate helper application for renderer, GPU and
  // utility subprocesses when running as an app bundle.
  std::filesystem::path executable_path = ExecutablePath();
  if (executable_path.empty() && argc > 0) executable_path = argv[0];
  // <exe> = <App>.app/Contents/MacOS/<binary>; parent² = <App>.app/Contents.
  const auto helper_path =
      executable_path.parent_path().parent_path() /
      "Frameworks/SHELTER Helper.app/Contents/MacOS/SHELTER Helper";
  CefString(&settings.browser_subprocess_path) = helper_path.string();
#endif
  // Required by CEF >= 120: absolute installation/profile root. Also enables
  // localStorage persistence for the SHELTER UI (cache_path == root).
  CefString(&settings.root_cache_path) = data_dir.string();
  CefString(&settings.cache_path) = data_dir.string();
  CefString(&settings.log_file) = (data_dir / "shelter-cef.log").string();

  if (!CefInitialize(args, settings, app, nullptr)) {
    shelter::Log(shelter::LogLevel::Error, "main: CefInitialize failed");
    return 1;
  }
  shelter::Log(shelter::LogLevel::Info, "main: CefInitialize returned");
  CefRunMessageLoop();
  CefShutdown();
  return 0;
}
