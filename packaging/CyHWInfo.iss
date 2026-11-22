#ifndef AppVersion
  #error AppVersion must be supplied by package-windows.ps1
#endif
#ifndef StageDir
  #error StageDir must be supplied by package-windows.ps1
#endif

[Setup]
AppId={{E67E701B-7C42-4432-91B2-B9478EF4FD7B}
AppName=CyHWInfo
AppVersion={#AppVersion}
AppPublisher=Cyberalien
AppPublisherURL=https://github.com/MrMybal/CyHWInfo
AppUpdatesURL=https://github.com/MrMybal/CyHWInfo/releases
DefaultDirName={localappdata}\Programs\CyHWInfo
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\bin\CyHWInfo.exe
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
VersionInfoVersion={#AppVersion}.0
OutputBaseFilename=CyHWInfo-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Files]
Source: "{#StageDir}\bin\*"; DestDir: "{app}\bin"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\share\CyHWInfo\LICENSE"; DestDir: "{app}\licenses"; DestName: "CyHWInfo-LICENSE.txt"; Flags: ignoreversion
Source: "{#StageDir}\share\CyHWInfo\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\CyHWInfo"; Filename: "{app}\bin\CyHWInfo.exe"

[Registry]
Root: HKCU; Subkey: "Software\CyHWInfo"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletevalue uninsdeletekeyifempty

[Run]
Filename: "{app}\bin\CyHWInfo.exe"; Flags: nowait runasoriginaluser; Check: IsAppUpdate
Filename: "{app}\bin\CyHWInfo.exe"; Description: "{cm:LaunchProgram,CyHWInfo}"; Flags: nowait postinstall skipifsilent; Check: not IsAppUpdate

[Code]
function IsAppUpdate: Boolean;
begin
  Result := ExpandConstant('{param:CYHWINFOUPDATE|0}') = '1';
end;
