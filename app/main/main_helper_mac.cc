#include "app/cef/cef_app.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"

#if defined(CEF_USE_SANDBOX)
#include "include/cef_sandbox_mac.h"
#endif

// This entry point is bundled as SHELTER Helper.app and is used only for CEF
// renderer, GPU and utility subprocesses. It must load CEF from the helper
// bundle's relative location, not from the main application's executable path.
int main(int argc, char* argv[]) {
#if defined(CEF_USE_SANDBOX)
  CefScopedSandboxContext sandbox_context;
  if (!sandbox_context.Initialize(argc, argv)) return 1;
#endif

  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInHelper()) return 1;

  CefMainArgs args(argc, argv);
  CefRefPtr<shelter::App> app(new shelter::App);
  return CefExecuteProcess(args, app, nullptr);
}
