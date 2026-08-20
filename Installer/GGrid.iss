; Inno Setup script for GGrid.
; Builds "Install GGrid.exe" -- lets the user pick Standalone app / VST3 plugin (or both) via
; checkboxes, and choose the VST3 install folder (defaulting to the standard system VST3
; folder), matching how MultibandConvolver's installer works.
;
; Build with: ISCC.exe Installer\GGrid.iss
; (run from the repo root, or adjust the relative Source paths below if not)

#define MyAppName "GGrid"
#define MyAppVersion "1.0.3"
#define MyAppPublisher "Mirror Maze"
#define MyBuildDir "..\build\GGrid_artefacts\Release"

[Setup]
AppId={{E3A5F2D1-9B4C-4A7E-8F1D-2C6B9A0E5D3F}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppPublisher}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\Installer\Output
OutputBaseFilename=Install GGrid
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
DisableWelcomePage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Standalone application and VST3 plugin"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "standalone"; Description: "Standalone application"; Types: full custom
Name: "vst3"; Description: "VST3 plugin"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut for the standalone app"; GroupDescription: "Additional icons:"; Components: standalone; Flags: unchecked

[Files]
Source: "{#MyBuildDir}\Standalone\GGrid.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone
Source: "{#MyBuildDir}\VST3\GGrid.vst3\*"; DestDir: "{code:GetVST3Dir}\GGrid.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
; Factory IR library -- installed once to a shared per-user location (not beside either binary)
; since IRLibrary::resolveIRRoot() checks {userdocs}\GGrid\IRs as its "user content-pack"
; fallback, and both Standalone and VST3 need to find the exact same files. Unconditional (not
; tied to a Component) since either app needs it to do anything useful with Convolution.
Source: "..\Resources\IRs\*"; DestDir: "{userdocs}\GGrid\IRs"; Flags: ignoreversion recursesubdirs createallsubdirs
; Bundled wavetable fallback library for LFO Table and WT Synth. User-installed Kilohearts tables
; can still be discovered separately, but fresh installs need this local fallback.
Source: "..\Resources\Wavetables\*"; DestDir: "{userdocs}\GGrid\Wavetables"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\GGrid.exe"; Components: standalone
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\GGrid.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\GGrid.exe"; Description: "Launch {#MyAppName} now"; Flags: nowait postinstall skipifsilent; Check: WizardIsComponentSelected('standalone')

[Code]
var
  VST3DirPage: TInputDirWizardPage;

procedure InitializeWizard;
begin
  VST3DirPage := CreateInputDirPage(wpSelectComponents,
    'Select VST3 Plugin Folder', 'Where should the VST3 plugin be installed?',
    'Setup will install the VST3 plugin in the following folder -- this is the standard system ' +
    'VST3 folder that DAWs scan by default. Only change this if you use a custom plugin folder.',
    False, '');
  VST3DirPage.Add('');
  VST3DirPage.Values[0] := ExpandConstant('{commoncf64}\VST3');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = VST3DirPage.ID then
    Result := not WizardIsComponentSelected('vst3');
end;

function GetVST3Dir(Param: String): String;
begin
  Result := VST3DirPage.Values[0];
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectComponents then
  begin
    if (not WizardIsComponentSelected('standalone')) and (not WizardIsComponentSelected('vst3')) then
    begin
      MsgBox('Please select at least one component to install.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;
