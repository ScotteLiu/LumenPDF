; Inno Setup script for LumenPDF.
;
; Built by scripts/package.ps1, which stages a self-contained directory with
; windeployqt first and passes its path in as StageDir. Nothing here reaches
; into the build tree directly -- staging is what guarantees the installer
; contains exactly what was tested.
;
; LGPL compliance note: Qt is deployed as separate DLLs and never statically
; linked, and LICENSES\ ships the third-party licence texts. Both are
; obligations of using Qt under LGPLv3 rather than a commercial licence.

#ifndef StageDir
  #define StageDir "..\build\stage"
#endif
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

#define AppName "LumenPDF"
#define AppPublisher "Lumen"
#define AppExeName "lumenpdf.exe"

[Setup]
AppId={{7A4E1C62-9B3D-4F1A-8C2E-5D9F0A3B6E41}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
OutputDir=..\build\dist
OutputBaseFilename=LumenPDF-{#AppVersion}-win64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; 64-bit only, and installs under Program Files rather than the 32-bit tree.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Per-user install needs no elevation; that is the friendlier default and
; avoids a UAC prompt for what is just a document viewer.
PrivilegesRequiredOverridesAllowed=dialog
PrivilegesRequired=lowest

; Shown before installation. BSL is not a licence people have memorised, so it
; has to be put in front of them rather than buried in a folder.
LicenseFile=..\LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate"; Description: "Open PDF files with {#AppName}"; GroupDescription: "File associations:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
; A ProgID of our own, then an entry under OpenWithProgids rather than seizing
; the .pdf default outright -- LumenPDF appears in "Open with" and the user
; chooses. Silently stealing the default association is hostile.
Root: HKA; Subkey: "Software\Classes\LumenPDF.Document"; ValueType: string; ValueName: ""; ValueData: "PDF Document"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\LumenPDF.Document\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExeName},0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\LumenPDF.Document\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: string; ValueName: "LumenPDF.Document"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""; Flags: uninsdeletekey

[Run]
; Qt's official binaries link the dynamic MSVC runtime, so it is a hard
; requirement -- not ours to opt out of by static linking. The redistributable
; no-ops within seconds when an equal or newer version is already present,
; which is why it runs unconditionally rather than behind a version check that
; would be one more thing to get wrong.
Filename: "{app}\vc_redist.x64.exe"; \
    Parameters: "/install /quiet /norestart"; \
    StatusMsg: "Installing the Microsoft Visual C++ runtime..."; \
    Flags: waituntilterminated skipifdoesntexist

Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
