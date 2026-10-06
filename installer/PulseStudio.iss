; Inno Setup script for Pulse Studio.
; Build:  ISCC.exe installer\PulseStudio.iss
; Override the build folder with  /DBuildDir=..\build-dev
#define AppName "Pulse Studio"
#define AppVersion "0.99.48"
#define AppStage "1.0 RC 1"
#ifndef BuildDir
  #define BuildDir "..\build"
#endif

[Setup]
AppId={{6C1F6F2E-3B8A-4D2B-9C1E-5A1D7B3E9F42}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion} ({#AppStage})
AppPublisher=Pulse
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} Setup
ChangesAssociations=yes
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
Name: "fileassoc"; Description: "Open Pulse Studio projects (.tracktionedit) with a double-click"; GroupDescription: "File types:"

[Files]
Source: "{#BuildDir}\PulseStudio_artefacts\Release\Pulse Studio.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD-PARTY-NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
Root: HKA; Subkey: "Software\Classes\.tracktionedit\OpenWithProgids"; ValueType: string; ValueName: "PulseStudio.Project"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\PulseStudio.Project"; ValueType: string; ValueName: ""; ValueData: "Pulse Studio Project"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\PulseStudio.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Pulse Studio.exe,0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\PulseStudio.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Pulse Studio.exe"" ""%1"""; Tasks: fileassoc

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\Pulse Studio.exe"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\Pulse Studio.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Pulse Studio.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
