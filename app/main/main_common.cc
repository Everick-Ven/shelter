#include "app/cef/cef_app.h"
#include "app/common/logging.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"
#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(__APPLE__)
#include <filesystem>
#endif
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
