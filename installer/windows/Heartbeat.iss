; Heartbeat installer (Inno Setup 6). Built by build_installer.bat, which passes the folders below.
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
; Paths are absolute (SourcePath is this script's folder) so the checks below work from any directory.
#ifndef PluginBuild
  #define PluginBuild SourcePath + "..\..\plugin\builds\vs17\x64\Release"
#endif
#ifndef AppExe
  #define AppExe SourcePath + "..\..\standalone\builds\vs17\x64\Release\App\Heartbeat.exe"
#endif
#ifndef ContentDir
  #define ContentDir SourcePath + "..\..\..\heartbeat-factory"
#endif
#ifndef PresetsDir
  #define PresetsDir SourcePath + "..\..\..\Presets"
#endif
#ifndef SamplesDir
  #define SamplesDir SourcePath + "..\..\..\Melodies"
#endif
#ifndef VcRedist
  #define VcRedist SourcePath + "vc_redist.x64.exe"
#endif

[Setup]
AppId={{6F1C2E4B-8B7D-4C55-9C1A-2B7E3D4F5A60}
AppName=Heartbeat
AppVersion={#AppVersion}
AppPublisher=Fabio
DefaultDirName={autopf}\Heartbeat
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputDir=..\out
OutputBaseFilename=Heartbeat-{#AppVersion}-Windows-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=Heartbeat

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin"; Types: full custom
Name: "standalone"; Description: "Standalone application"; Types: full custom
Name: "content"; Description: "Factory presets, wavetables and LFOs"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Components: standalone; Flags: unchecked

[Files]
; JUCE writes the VST3 either as a single file or as a bundle folder, depending on the version.
#if DirExists(PluginBuild + "\VST3\Heartbeat.vst3")
Source: "{#PluginBuild}\VST3\Heartbeat.vst3\*"; DestDir: "{commoncf64}\VST3\Heartbeat.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
#else
Source: "{#PluginBuild}\VST3\Heartbeat.vst3"; DestDir: "{commoncf64}\VST3"; Components: vst3; Flags: ignoreversion
#endif
Source: "{#AppExe}"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
; Factory packs go where Heartbeat looks for every user's factory content (ProgramData\Heartbeat).
Source: "{#ContentDir}\*"; Excludes: "tools,install.bat"; DestDir: "{commonappdata}\Heartbeat"; Components: content; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PresetsDir}\*.vital"; DestDir: "{commonappdata}\Heartbeat\Factory\Presets"; Components: content; Flags: ignoreversion
Source: "{#SamplesDir}\*.wav"; DestDir: "{commonappdata}\Heartbeat\Melodies\Samples"; Components: content; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#VcRedist}"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: VCRedistNeeded

[Icons]
Name: "{autoprograms}\Heartbeat"; Filename: "{app}\Heartbeat.exe"; Components: standalone
Name: "{autodesktop}\Heartbeat"; Filename: "{app}\Heartbeat.exe"; Components: standalone; Tasks: desktopicon

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Visual C++ runtime..."; Check: VCRedistNeeded; Flags: waituntilterminated
Filename: "{app}\Heartbeat.exe"; Description: "Launch Heartbeat"; Components: standalone; Flags: nowait postinstall skipifsilent unchecked

[Code]
// Heartbeat is built against the Visual C++ 2015-2022 runtime (x64); install it only when it's missing.
function VCRedistNeeded: Boolean;
var
  Installed: Cardinal;
begin
  Result := not (RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed)
                 and (Installed = 1));
end;
