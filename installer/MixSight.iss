; MixSight installer (Inno Setup 6). Built by CI:  ISCC /DMyVersion=0.5.0 /DDist=..\dist\MixSight installer\MixSight.iss
#ifndef MyVersion
  #define MyVersion "0.0.0"
#endif
#ifndef Dist
  #define Dist "..\dist\MixSight"
#endif

[Setup]
AppId={{6E4C2B7A-3D41-4E8B-9C2F-5A7D1B0E9F31}
AppName=MixSight
AppVersion={#MyVersion}
AppVerName=MixSight {#MyVersion}
AppPublisher=MixSight
AppPublisherURL=https://github.com/miroslavlacek/keeco-mix-feedback
DefaultDirName={commoncf64}\VST3\MixSight
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=MixSight-Setup-{#MyVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=MixSight {#MyVersion}
CloseApplications=no
SetupLogging=yes

[Languages]
Name: "sk"; MessagesFile: "compiler:Languages\Slovak.isl"

[CustomMessages]
sk.TaskAi=Pripraviť AI chat (Gemini cez Antigravity CLI – stiahne sa z internetu)
sk.RunSignIn=Prihlásiť sa do Gemini teraz (otvorí okno Antigravity a prehliadač)
sk.StatusAgy=Inštalujem Antigravity CLI...
sk.StatusAi=Pripájam AI k pluginu...

[Tasks]
Name: "ai"; Description: "{cm:TaskAi}"

[Dirs]
; Plugin folder writable for normal users: updates from the plugin then need no admin rights.
Name: "{app}"; Permissions: users-modify
Name: "{app}\captures"; Permissions: users-modify

[InstallDelete]
; old "Keeco Mix Feedback" installation (same plugin ID – two copies would confuse the DAW)
Type: filesandordirs; Name: "{commoncf64}\VST3\Keeco Mix Feedback.vst3"
Type: files; Name: "C:\KeecoMixFeedback\KeecoMcpBridge.exe"
Type: files; Name: "C:\KeecoMixFeedback\GEMINI.md"
Type: filesandordirs; Name: "C:\KeecoMixFeedback\captures"
Type: dirifempty; Name: "C:\KeecoMixFeedback"

[Files]
Source: "{#Dist}\MixSight.vst3\*"; DestDir: "{app}\MixSight.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Dist}\MixSightBridge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Dist}\MixSightHelper.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Dist}\GEMINI.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Dist}\NAVOD.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "install-agy.ps1"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Run]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{tmp}\install-agy.ps1"""; StatusMsg: "{cm:StatusAgy}"; Tasks: ai; Flags: runasoriginaluser runhidden waituntilterminated
Filename: "{app}\MixSightHelper.exe"; Parameters: "--setup-ai"; StatusMsg: "{cm:StatusAi}"; Tasks: ai; Flags: runasoriginaluser runhidden waituntilterminated
Filename: "{app}\MixSightHelper.exe"; Parameters: "--sign-in"; Description: "{cm:RunSignIn}"; Tasks: ai; Flags: postinstall runasoriginaluser nowait skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\captures"
Type: files; Name: "{app}\*.old-*"
Type: files; Name: "{app}\MixSight.vst3\Contents\x86_64-win\*.old-*"
Type: dirifempty; Name: "{app}"

[Code]
// A DAW that is running keeps the plugin DLL (and maybe the bridge) locked. Windows does allow
// renaming such files, so move them aside; the plugin deletes the *.old-N leftovers on its next start.
procedure MoveAside(const FileName: String);
var
  N: Integer;
begin
  if not FileExists(FileName) then Exit;
  if DeleteFile(FileName) then Exit;
  N := 1;
  while FileExists(FileName + '.old-' + IntToStr(N)) do N := N + 1;
  RenameFile(FileName, FileName + '.old-' + IntToStr(N));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  MoveAside(ExpandConstant('{app}\MixSight.vst3\Contents\x86_64-win\MixSight.vst3'));
  MoveAside(ExpandConstant('{app}\MixSightBridge.exe'));
  MoveAside(ExpandConstant('{app}\MixSightHelper.exe'));
  Result := '';
end;
