#include "app/cef/cef_app.h"
#include "app/common/logging.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"
#if defined(_WIN32)
#include <windows.h>
#endif
int main(int argc, char** argv) {
  shelter::InitializeLogging("shelter.log");
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInMain()) return 1;
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
  CefString(&settings.log_file) = "shelter-cef.log";
  if (!CefInitialize(args, settings, app, nullptr)) return 1;
  CefRunMessageLoop();
  CefShutdown();
  return 0;
}
