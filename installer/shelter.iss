; SHELTER — установщик для Windows x64 (Inno Setup 6).
; Сборка передаёт AppVersion из CMakeLists.txt через /DAppVersion.
; Установка на пользователя (без прав администратора): %LOCALAPPDATA%\Programs\SHELTER.
; Данные профиля (%LOCALAPPDATA%\SHELTER) при удалении программы НЕ стираются.

#ifndef AppVersion
  #error AppVersion must be supplied from the configured CMake version
#endif
#ifndef SourceDir
  #define SourceDir "..\build\Release"
#endif

[Setup]
AppId={{7C1E4B3A-5D0F-4C0B-9A57-3E2B8F1D6A90}
AppName=SHELTER
AppVersion={#AppVersion}
AppVerName=SHELTER {#AppVersion}
AppPublisher=Golubev & Kulkov
VersionInfoVersion={#AppVersion}.0
DefaultDirName={autopf}\SHELTER
DefaultGroupName=SHELTER
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=SHELTER-Setup-x64
SetupIconFile=..\win\shelter.ico
UninstallDisplayIcon={app}\Shelter.exe
UninstallDisplayName=SHELTER
Compression=lzma2/normal
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "*.pdb,*.lib,*.exp"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\SHELTER"; Filename: "{app}\Shelter.exe"
Name: "{autodesktop}\SHELTER"; Filename: "{app}\Shelter.exe"; Tasks: desktopicon

[Run]
; LPAC sandboxed CEF renderers need read/execute access to the installed bundle.
Filename: "{sys}\icacls.exe"; Parameters: """{app}"" /grant *S-1-15-2-2:(OI)(CI)(RX) /T"; StatusMsg: "Подготовка песочницы Chromium..."; Flags: runhidden waituntilterminated
Filename: "{app}\Shelter.exe"; Description: "{cm:LaunchProgram,SHELTER}"; Flags: nowait postinstall skipifsilent
