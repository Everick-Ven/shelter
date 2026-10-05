#include "app/cef/cef_app.h"
#include "app/common/logging.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#if defined(CEF_USE_SANDBOX)
#include "include/cef_sandbox_mac.h"
#endif

namespace {
namespace fs = std::filesystem;

std::string PathToUtf8(const fs::path& path) {
  const auto value = path.u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

fs::path GetLogDirectory() {
  if (const char* home = std::getenv("HOME")) {
    return fs::path(home) / "Library" / "Application Support" / "SHELTER";
  }
  return fs::temp_directory_path() / "SHELTER";
}

bool IsHelperLoaderSmokeTest(int argc, char* argv[]) {
  constexpr std::string_view kSwitch = "--shelter-helper-smoke-test";
  for (int i = 1; i < argc; ++i) {
    if (argv[i] && std::string_view(argv[i]) == kSwitch) return true;
  }
  return false;
}

}  // namespace

// This entry point is bundled as SHELTER Helper.app and is used only for CEF
// renderer, GPU and utility subprocesses. It must load CEF from the helper
// bundle's relative location, not from the main application's executable path.
int main(int argc, char* argv[]) {
  std::error_code error;
  fs::path log_directory = GetLogDirectory();
  fs::create_directories(log_directory, error);
  if (error) {
    error.clear();
    log_directory = fs::temp_directory_path() / "SHELTER";
    fs::create_directories(log_directory, error);
  }
  const std::string log_path = PathToUtf8(log_directory / "shelter-helper.log");
  shelter::InitializeLogging(log_path.c_str());

#if defined(CEF_USE_SANDBOX)
  CefScopedSandboxContext sandbox_context;
  if (!sandbox_context.Initialize(argc, argv)) {
    shelter::Log(shelter::LogLevel::Error,
                 "CEF macOS sandbox initialization failed in helper process");
    return 1;
  }
#endif

  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInHelper()) {
    shelter::Log(shelter::LogLevel::Error,
                 "Failed to load the CEF framework in helper process");
    return 1;
  }

  if (IsHelperLoaderSmokeTest(argc, argv)) {
    shelter::Log(shelter::LogLevel::Info,
                 "CEF helper framework load smoke test passed");
    return 0;
  }

  CefMainArgs args(argc, argv);
  CefRefPtr<shelter::App> app(new shelter::App);
  std::string process_type = "browser";
  for (int i = 1; i < argc; ++i) {
    if (argv[i] && std::string_view(argv[i]).rfind("--type=", 0) == 0) {
      process_type = argv[i] + std::string_view("--type=").size();
      break;
    }
  }
  if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
    shelter::Log(shelter::LogLevel::Info,
                 "SHELTER_WEB_SMOKE_HELPER_PROCESS_STARTED type=" +
                     process_type);
  }
  shelter::Log(shelter::LogLevel::Info,
               "CEF helper loaded the framework; dispatching subprocess");
  const int exit_code = CefExecuteProcess(args, app, nullptr);
  shelter::Log(shelter::LogLevel::Info,
               "CEF helper subprocess exited with status " +
                   std::to_string(exit_code));
  if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
    shelter::Log(shelter::LogLevel::Info,
                 "SHELTER_WEB_SMOKE_HELPER_PROCESS_EXIT type=" +
                     process_type + " code=" + std::to_string(exit_code));
  }
  if (exit_code < 0) {
    shelter::Log(shelter::LogLevel::Error,
                 "CEF helper received no recognized subprocess command");
  }
  return exit_code;
}
