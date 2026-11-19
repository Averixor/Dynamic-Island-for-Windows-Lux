; Inno Setup script — Dynamic Island for Windows Lux
; Build: scripts\make-installer.bat  (requires Inno Setup 6+)

#ifndef MyAppVersion
  #define MyAppVersion "1.0.6"
#endif

#define MyAppName "Dynamic Island for Windows Lux"
#define MyAppNameShort "Dynamic Island Lux"
#define MyAppPublisher "Averixor"
#define MyAppExeName "DynamicIslandLux.exe"
#define MyAppId "{{A7C4E2F1-9B8D-4E6A-9F31-2D5C8B0A1E77}"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppCopyright=Copyright (c) 2026 devcode90. Copyright (c) Averixor and contributors
AppPublisherURL=https://github.com/Averixor/Dynamic-Island-for-Windows-Lux
AppSupportURL=https://github.com/Averixor/Dynamic-Island-for-Windows-Lux/issues
AppUpdatesURL=https://github.com/Averixor/Dynamic-Island-for-Windows-Lux/releases
DefaultDirName={localappdata}\DynamicIslandLux
DefaultGroupName={#MyAppNameShort}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
InfoBeforeFile=..\INSTALL.txt
OutputDir=..\dist\v{#MyAppVersion}
OutputBaseFilename=DynamicIslandLux-Setup-{#MyAppVersion}
SetupIconFile=..\app\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
CloseApplications=yes
RestartApplications=no
AllowNoIcons=yes
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoCopyright=Copyright (c) 2026 devcode90. Copyright (c) Averixor and contributors
VersionInfoDescription={#MyAppName} Setup
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Start with Windows (HKCU Run)"; GroupDescription: "Startup:"; Flags: unchecked

[Files]
Source: "..\build\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\NOTICE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\INSTALL.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\RELEASE_NOTES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppNameShort}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{group}\Settings"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--settings"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,{#MyAppNameShort}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppNameShort}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; Same value name as app\autostart.cpp (DynamicIslandLux).
; --enable-autostart is consumed on first launch and rewritten to "--minimized" only.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "DynamicIslandLux"; ValueData: """{app}\{#MyAppExeName}"" --minimized --enable-autostart"; Flags: uninsdeletevalue; Tasks: autostart
; ValueType none: do not create StartupApproved, but delete it on uninstall if the app wrote it.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run"; ValueType: none; ValueName: "DynamicIslandLux"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppNameShort}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Leave user settings/logs; only remove empty install dir leftovers.
Type: dirifempty; Name: "{app}"

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    { App-created Run / StartupApproved values are not tracked by [Registry] Tasks. }
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', 'DynamicIslandLux');
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run', 'DynamicIslandLux');
  end;
end;
