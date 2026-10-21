// SHELTER — точка входа Windows (браузерный и все вспомогательные процессы).
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <string>
#include <vector>

#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_version_info.h"
#include "src/browser_app.h"
#include "src/renderer_app.h"
#include "src/platform.h"
#include "src/shell.h"

namespace {

#if defined(CEF_USE_SANDBOX)
bool HasLpacBundleAccess(const std::wstring& bundle_path) {
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const DWORD security_result = GetNamedSecurityInfoW(
      bundle_path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
      nullptr, &dacl, nullptr, &descriptor);
  if (security_result != ERROR_SUCCESS) return false;

  PSID lpac_sid = nullptr;
  if (!ConvertStringSidToSidW(L"S-1-15-2-2", &lpac_sid)) {
    LocalFree(descriptor);
    return false;
  }

  bool has_inheritable_read_execute = false;
  if (dacl != nullptr) {
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
      void* raw_ace = nullptr;
      if (!GetAce(dacl, index, &raw_ace)) continue;
      const auto* header = static_cast<const ACE_HEADER*>(raw_ace);
      if (header->AceType != ACCESS_ALLOWED_ACE_TYPE ||
          (header->AceFlags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) !=
              (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) {
        continue;
      }
      const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
      const DWORD required_access = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
      if ((ace->Mask & required_access) == required_access &&
          IsEqualSid(const_cast<DWORD*>(&ace->SidStart), lpac_sid)) {
        has_inheritable_read_execute = true;
        break;
      }
    }
  }
  LocalFree(lpac_sid);
  LocalFree(descriptor);
  return has_inheritable_read_execute;
}

bool EnsureLpacBundleAccess() {
  std::vector<wchar_t> module_path(32768, 0);
  const DWORD module_length = GetModuleFileNameW(
      nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
  if (module_length == 0 || module_length >= module_path.size()) return false;
  std::wstring bundle_path(module_path.data(), module_length);
  const size_t separator = bundle_path.find_last_of(static_cast<wchar_t>(92));
  if (separator == std::wstring::npos) return false;
  bundle_path.resize(separator);
  if (HasLpacBundleAccess(bundle_path)) return true;

  std::vector<wchar_t> system_dir(32768, 0);
  const UINT system_length = GetSystemDirectoryW(
      system_dir.data(), static_cast<UINT>(system_dir.size()));
  if (system_length == 0 || system_length >= system_dir.size()) return false;

  std::wstring command_line;
  command_line.push_back(static_cast<wchar_t>(34));
  command_line.append(system_dir.data(), system_length);
  command_line.push_back(static_cast<wchar_t>(92));
  command_line += L"icacls.exe";
  command_line.push_back(static_cast<wchar_t>(34));
  command_line.push_back(L' ');
  command_line.push_back(static_cast<wchar_t>(34));
  command_line += bundle_path;
  command_line.push_back(static_cast<wchar_t>(34));
  command_line += L" /grant *S-1-15-2-2:(OI)(CI)(RX) /T";
  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(0);

  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info{};
  if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup_info,
                      &process_info)) {
    return false;
  }

  const DWORD wait_result = WaitForSingleObject(process_info.hProcess, 30000);
  DWORD exit_code = 1;
  if (wait_result == WAIT_OBJECT_0) {
    GetExitCodeProcess(process_info.hProcess, &exit_code);
  } else {
    TerminateProcess(process_info.hProcess, 1);
  }
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  return wait_result == WAIT_OBJECT_0 && exit_code == 0;
}
#endif

int RunMain(HINSTANCE hInstance, LPWSTR lpCmdLine, int nCmdShow,
            void* sandbox_info) {
  UNREFERENCED_PARAMETER(lpCmdLine);
  UNREFERENCED_PARAMETER(nCmdShow);

  CefMainArgs main_args(hInstance);
  CefRefPtr<CefCommandLine> cl = CefCommandLine::CreateCommandLine();
  cl->InitFromString(::GetCommandLineW());
  const bool is_browser = cl->GetSwitchValue("type").empty();
#if defined(CEF_USE_SANDBOX)
  if (is_browser && !EnsureLpacBundleAccess()) {
    MessageBoxW(nullptr,
                L"Не удалось подготовить права для песочницы Chromium. "
                L"Проверьте права на папку SHELTER и повторите запуск.",
                L"SHELTER", MB_OK | MB_ICONERROR);
    return 1;
  }
#endif

  CefRefPtr<CefApp> app;
  if (is_browser) {
    app = new shelter::BrowserApp();
  } else {
    app = new shelter::RendererApp();
  }

  const int exit_code = CefExecuteProcess(main_args, app, sandbox_info);
  if (exit_code >= 0) return exit_code;

  CefSettings settings;
  shelter::FillSettings(settings);
  if (!CefInitialize(main_args, settings, app, sandbox_info)) {
    return CefGetExitCode();
  }

  CefRunMessageLoop();
  const bool delete_user_data = shelter::Shell::Get().delete_user_data_on_exit();
  shelter::Shell::Get().PrepareForShutdown();
  CefShutdown();
  if (delete_user_data && !shelter::platform::DeleteUserData())
    shelter::platform::ShowUserDataDeletionFailure();
  return 0;
}

}  // namespace

#if defined(CEF_USE_BOOTSTRAP)
// CEF M138+ bootstrap.exe loads Shelter.dll and supplies sandbox_info.
CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE hInstance, LPWSTR lpCmdLine,
                                    int nCmdShow, void* sandbox_info,
                                    cef_version_info_t* /*version_info*/) {
  return RunMain(hInstance, lpCmdLine, nCmdShow, sandbox_info);
}
#else
// Compatibility path for an explicitly sandbox-disabled build.
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine,
                      int nCmdShow) {
  void* sandbox_info = nullptr;
#if defined(CEF_USE_SANDBOX)
  CefScopedSandboxInfo scoped_sandbox;
  sandbox_info = scoped_sandbox.sandbox_info();
#endif
  return RunMain(hInstance, lpCmdLine, nCmdShow, sandbox_info);
}
#endif
