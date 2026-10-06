// SHELTER — вспомогательные процессы macOS (renderer / GPU / plugin / alerts).
#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"
#include "src/renderer_app.h"

#if defined(CEF_USE_SANDBOX)
#include "include/cef_sandbox_mac.h"
#endif

int main(int argc, char* argv[]) {
#if defined(CEF_USE_SANDBOX)
  // CEF's macOS sandbox must be initialized before loading the framework.
  CefScopedSandboxContext sandbox_context;
  if (!sandbox_context.Initialize(argc, argv)) return 1;
#endif

  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInHelper()) return 1;

  CefMainArgs main_args(argc, argv);
  CefRefPtr<CefApp> app(new shelter::RendererApp());
  return CefExecuteProcess(main_args, app, nullptr);
}
