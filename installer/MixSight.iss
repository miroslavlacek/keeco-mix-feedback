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
sk.SignInTitle=Prihlásenie do Gemini
sk.SignInSub=AI chat v pluginu používa tvoj Google účet (bezplatná denná kvóta).
sk.SignInNeeded=Na AI chat sa treba raz prihlásiť Google účtom.%n%nPo kliknutí na tlačidlo sa otvorí okno Antigravity a prehliadač. V okne vyber „Login with Google“ a prihlás sa v prehliadači. Okno sa po prihlásení zavrie samo.%n%nMôžeš to aj preskočiť a prihlásiť sa neskôr v pluginu (AI chat → Prihlásiť sa cez Google).
sk.SignInButton=Prihlásiť sa cez Google
sk.SignInWaiting=Čakám na prihlásenie… Dokonči ho v okne Antigravity a v prehliadači.
sk.SignInDone=✓ Si prihlásený do Gemini. AI chat v pluginu je pripravený.
sk.SignInFailed=Prihlásenie sa nedokončilo. Skús to znova, alebo neskôr v pluginu (AI chat → Prihlásiť sa cez Google).
sk.SignInNoAgy=Antigravity CLI sa nepodarilo nainštalovať (skontroluj pripojenie na internet). AI chat dokončíš neskôr priamo v pluginu – záložka AI chat ťa prevedie.
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

[UninstallDelete]
Type: filesandordirs; Name: "{app}\captures"
Type: files; Name: "{app}\*.old-*"
Type: files; Name: "{app}\MixSight.vst3\Contents\x86_64-win\*.old-*"
Type: dirifempty; Name: "{app}"

[Code]
var
  SignInPage: TWizardPage;
  SignInText: TNewStaticText;
  SignInButton: TNewButton;

function HelperExe: String;
begin
  Result := ExpandConstant('{app}\MixSightHelper.exe');
end;

// 0 = signed in, 1 = not signed in, 2 = Antigravity missing (see MixSightHelper --signed-in)
function SignInState: Integer;
var
  Code: Integer;
begin
  if not ExecAsOriginalUser(HelperExe, '--signed-in', '', SW_HIDE, ewWaitUntilTerminated, Code) then Code := 2;
  Result := Code;
end;

procedure ShowSignInState(State: Integer);
begin
  SignInButton.Visible := State = 1;
  SignInButton.Enabled := True;
  if State = 0 then SignInText.Caption := CustomMessage('SignInDone')
  else if State = 2 then SignInText.Caption := CustomMessage('SignInNoAgy')
  else SignInText.Caption := CustomMessage('SignInNeeded');
end;

procedure SignInButtonClick(Sender: TObject);
var
  Code: Integer;
begin
  SignInButton.Enabled := False;
  SignInText.Caption := CustomMessage('SignInWaiting');
  WizardForm.NextButton.Enabled := False;
  ExecAsOriginalUser(HelperExe, '--sign-in', '', SW_HIDE, ewWaitUntilTerminated, Code);
  WizardForm.NextButton.Enabled := True;
  if SignInState = 0 then ShowSignInState(0)
  else begin
    ShowSignInState(1);
    SignInText.Caption := CustomMessage('SignInFailed');
  end;
end;

procedure InitializeWizard;
begin
  SignInPage := CreateCustomPage(wpInfoAfter, CustomMessage('SignInTitle'), CustomMessage('SignInSub'));
  SignInText := TNewStaticText.Create(SignInPage);
  SignInText.Parent := SignInPage.Surface;
  SignInText.AutoSize := False;
  SignInText.WordWrap := True;
  SignInText.SetBounds(0, 0, SignInPage.SurfaceWidth, ScaleY(120));
  SignInButton := TNewButton.Create(SignInPage);
  SignInButton.Parent := SignInPage.Surface;
  SignInButton.Caption := CustomMessage('SignInButton');
  SignInButton.SetBounds(0, ScaleY(130), ScaleX(220), ScaleY(28));
  SignInButton.OnClick := @SignInButtonClick;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (SignInPage <> nil) and (PageID = SignInPage.ID) and not WizardIsTaskSelected('ai');
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (SignInPage <> nil) and (CurPageID = SignInPage.ID) then ShowSignInState(SignInState);
end;

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
