// Copy the compiled version resource from Shelter.dll into CEF's bootstrap exe.
// The sandbox build target is a DLL; without this step Explorer shows CEF's
// bootstrap version on Shelter.exe even though Shelter.dll has our resource.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>

#include <iostream>

namespace {

struct LanguageResult {
  WORD language = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
  bool found = false;
};

BOOL CALLBACK FirstLanguage(HMODULE, LPCWSTR, LPCWSTR, WORD language,
                            LONG_PTR parameter) {
  auto* result = reinterpret_cast<LanguageResult*>(parameter);
  result->language = language;
  result->found = true;
  return TRUE;
}

int Fail(const wchar_t* operation) {
  std::wcerr << L"ShelterVersionUpdater: " << operation
             << L" failed (Win32 " << GetLastError() << L")\n";
  return 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) {
    std::wcerr << L"usage: ShelterVersionUpdater <version-resource.dll> <bootstrap.exe>\n";
    return 2;
  }

  HMODULE source = LoadLibraryExW(
      argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  if (!source) return Fail(L"LoadLibraryExW");

  HRSRC resource = FindResourceW(source, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));
  if (!resource) {
    FreeLibrary(source);
    return Fail(L"FindResourceW");
  }
  const DWORD size = SizeofResource(source, resource);
  HGLOBAL loaded = LoadResource(source, resource);
  const void* bytes = loaded ? LockResource(loaded) : nullptr;
  if (!size || !bytes) {
    FreeLibrary(source);
    return Fail(L"LockResource");
  }

  LanguageResult language;
  if (!EnumResourceLanguagesW(source, MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1),
                              FirstLanguage,
                              reinterpret_cast<LONG_PTR>(&language)) ||
      !language.found) {
    FreeLibrary(source);
    return Fail(L"EnumResourceLanguagesW");
  }

  HANDLE update = BeginUpdateResourceW(argv[2], FALSE);
  if (!update) {
    FreeLibrary(source);
    return Fail(L"BeginUpdateResourceW");
  }
  const BOOL updated = UpdateResourceW(
      update, MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1), language.language,
      const_cast<void*>(bytes), size);
  const DWORD update_error = updated ? ERROR_SUCCESS : GetLastError();
  const BOOL committed = EndUpdateResourceW(update, updated ? FALSE : TRUE);
  FreeLibrary(source);
  if (!updated) {
    SetLastError(update_error);
    return Fail(L"UpdateResourceW");
  }
  if (!committed) return Fail(L"EndUpdateResourceW");
  return 0;
}
