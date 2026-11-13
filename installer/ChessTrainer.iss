#define Root ".."
#include Root + "\build\version.iss"

[Setup]
AppId={{6F2A8C1E-3B7D-4E55-9A1C-2D8E4F7B9C30}
AppName=Шахматный тренер
AppVersion={#AppVer}
AppPublisher=Шахматный тренер
DefaultDirName={autopf}\Шахматный тренер
DefaultGroupName=Шахматный тренер
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir={#Root}\dist
OutputBaseFilename=ChessTrainer-Setup
SetupIconFile={#Root}\assets\icon\app.ico
UninstallDisplayIcon={app}\ChessTrainer.exe
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
CloseApplications=force

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#Root}\build\Release\ChessTrainer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs
Source: "{#Root}\third_party\stockfish\*"; DestDir: "{app}\stockfish"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{autoprograms}\Шахматный тренер"; Filename: "{app}\ChessTrainer.exe"
Name: "{autodesktop}\Шахматный тренер"; Filename: "{app}\ChessTrainer.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\ChessTrainer.exe"; Description: "{cm:LaunchProgram,Шахматный тренер}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\ChessTrainer.exe"; Flags: nowait skipifnotsilent
