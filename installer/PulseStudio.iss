; Inno Setup script for Pulse Studio.
; Build:  ISCC.exe installer\PulseStudio.iss
; Override the build folder with  /DBuildDir=..\build-dev
#define AppName "Pulse Studio"
#define AppVersion "0.11.0"
#ifndef BuildDir
  #define BuildDir "..\build"
#endif

[Setup]
AppId={{6C1F6F2E-3B8A-4D2B-9C1E-5A1D7B3E9F42}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Pulse
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\Pulse Studio.exe
OutputDir=..\dist
OutputBaseFilename=PulseStudio-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
; Lets the user pick "install for me only" (no admin) or "for all users"
PrivilegesRequiredOverridesAllowed=dialog
PrivilegesRequired=lowest

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#BuildDir}\PulseStudio_artefacts\Release\Pulse Studio.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\Pulse Studio.exe"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\Pulse Studio.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Pulse Studio.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
