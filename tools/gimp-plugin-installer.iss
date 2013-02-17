;Installer for additional Gimp plugins
;
;supports both old-style (GIMP 2.6.x and older) and new-style (combined 32 and 64-bit) GIMP installations
;
;TODO: 64-bit GIMP can be installed without 32-bit plugin support - add code to detect this
;
;.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,
;                                                                       ;
;Copyright (c) 2002-2010 Jernej SimonŸiŸ                                ;
;                                                                       ;
;This software is provided 'as-is', without any express or implied      ;
;warranty. In no event will the authors be held liable for any damages  ;
;arising from the use of this software.                                 ;
;                                                                       ;
;Permission is granted to anyone to use this software for any purpose,  ;
;including commercial applications, and to alter it and redistribute it ;
;freely, subject to the following restrictions:                         ;
;                                                                       ;
;   1. The origin of this software must not be misrepresented; you must ;
;      not claim that you wrote the original software. If you use this  ;
;      software in a product, an acknowledgment in the product          ;
;      documentation would be appreciated but is not required.          ;
;                                                                       ;
;   2. Altered source versions must be plainly marked as such, and must ;
;      not be misrepresented as being the original software.            ;
;                                                                       ;
;   3. This notice may not be removed or altered from any source        ;
;      distribution.                                                    ;
;.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.;


;for testing
;#define dontcompress

;AppId - only used with new-style installs; should be unique
#define APP_ID="GIMP-Lensfun-Plugin"
#define VERSION="0.2.2"

[Setup]
AppName=Lensfun plug-in for GIMP
AppVerName=GimpLensfun plugin 0.2.2
AppPublisherURL=http://lensfun.sebastiankraft.net/
AppSupportURL=http://lensfun.sebastiankraft.net/
AppUpdatesURL=http://lensfun.sebastiankraft.net/
DefaultDirName={code:GetGimpDir|{pf}\GIMP 2}
;WizardImageFile=big.bmp
;WizardImageBackColor=$ffffff
;WizardSmallImageFile=small.bmp
;WizardImageStretch=no
FlatComponentsList=yes

;don't change these
DirExistsWarning=no
DisableProgramGroupPage=yes
DisableDirPage=no
CreateUninstallRegKey=no
UpdateUninstallLogAppName=no
AppID={code:GetAppId}
UninstallFilesDir={code:GetUninstallDir}
UsePreviousLanguage=no

#if defined(dontcompress)
UseSetupLdr=no
OutputDir=_Output\47z
Compression=none
InternalCompressLevel=0
#else
OutputDir=_Output
Compression=lzma/ultra
InternalCompressLevel=ultra
SolidCompression=yes
OutputBaseFilename=gimplensfun-{#VERSION}-setup
#endif

;SignedUninstaller=yes
;SignedUninstallerDir=_Uninst

;[Languages]
;Name: "en"; MessagesFile: "compiler:Default.isl,help.en.isl"

[Files]
; The plugin
Source: "../gimp-lensfun.exe"; DestDir: "{app}\lib\gimp\2.0\plug-ins"; Flags: ignoreversion

; Several libraries not already part of GIMP installation
Source: "C:\MinGW\bin\libexiv2-12.dll"; DestDir: "{app}\bin"; Flags: ignoreversion    
Source: "C:\MinGW\bin\liblensfun.dll"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "C:\MinGW\bin\libiconv-2.dll"; DestDir: "{app}\bin"; Flags: ignoreversion
;Source: "C:\MinGW\bin\libstdc++-6.dll"; DestDir: "{app}\bin"; Flags: ignoreversion

; For OpenMP
;Source: "C:\MinGW\bin\libgomp-1.dll"; DestDir: "{app}\bin"; Flags: ignoreversion    
;Source: "C:\MinGW\bin\libpthread-2.dll"; DestDir: "{app}\bin"; Flags: ignoreversion  

; The lensfun database xml files
Source: "C:\MinGW\share\lensfun\*"; DestDir: "{app}\share\lensfun\"; Flags: ignoreversion


[Code]
function SHAutoComplete(hWnd: Integer; dwFlags: DWORD): Integer; external 'SHAutoComplete@shlwapi.dll stdcall delayload';

function WideCharToMultiByte(CodePage: Cardinal; dwFlags: DWORD; lpWideCharStr: String; cchWideCharStr: Integer;
                             lpMultiByteStr: PAnsiChar; cbMultiByte: Integer; lpDefaultChar: Integer;
                             lpUsedDefaultChar: Integer): Integer; external 'WideCharToMultiByte@Kernel32 stdcall';

function MultiByteToWideChar(CodePage: Cardinal; dwFlags: DWORD; lpMultiByteStr: PAnsiChar; cbMultiByte: Integer;
                             lpWideCharStr: String; cchWideChar: Integer): Integer;
                             external 'MultiByteToWideChar@Kernel32 stdcall';

function GetLastError(): DWORD; external 'GetLastError@Kernel32 stdcall';

const
	SHACF_FILESYSTEM = $1;
	CP_UTF8 = 65001;

	RegPath = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\WinGimp-2.0_is1';
	RegPathNew = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\GIMP-2_is1';
	RegKey = 'Inno Setup: App Path';
	VerKey = 'DisplayName';

var
	GimpPath: String;
	GimpVer: String;

	InstallType: (itOld, itNew, itNew64);

function Count(What, Where: String): Integer;
begin
	Result := 0;
	if Length(What) = 0 then //nothing to count - should this throw an error?
		exit;

	while Pos(What,Where)>0 do
	begin
		Where := Copy(Where,Pos(What,Where)+Length(What),Length(Where));
		Result := Result + 1;
	end;
end;


//split text to array
procedure Explode(var ADest: TArrayOfString; aText, aSeparator: String);
var tmp: Integer;
begin
	if aSeparator='' then
		exit;

	SetArrayLength(ADest,Count(aSeparator,aText)+1)

	tmp := 0;
	repeat
		if Pos(aSeparator,aText)>0 then
		begin

			ADest[tmp] := Copy(aText,1,Pos(aSeparator,aText)-1);
			aText := Copy(aText,Pos(aSeparator,aText)+Length(aSeparator),Length(aText));
			tmp := tmp + 1;

		end else
		begin

			 ADest[tmp] := aText;
			 aText := '';

		end;
	until Length(aText)=0;
end;



//compares two version numbers, returns -1 if vA is newer, 0 if both are identical, 1 if vB is newer
function CompareVersion(vA,vB: String): Integer;
var tmp: TArrayOfString;
	verA,verB: Array of Integer;
	i,len: Integer;
begin

	StringChange(vA,'-','.');
	StringChange(vB,'-','.');

	Explode(tmp,vA,'.');
	SetArrayLength(verA,GetArrayLength(tmp));
	for i := 0 to GetArrayLength(tmp) - 1 do
		verA[i] := StrToIntDef(tmp[i],0);
		
	Explode(tmp,vB,'.');
	SetArrayLength(verB,GetArrayLength(tmp));
	for i := 0 to GetArrayLength(tmp) - 1 do
		verB[i] := StrToIntDef(tmp[i],0);

	len := GetArrayLength(verA);
	if GetArrayLength(verB) < len then
		len := GetArrayLength(verB);

	for i := 0 to len - 1 do
		if verA[i] < verB[i] then
		begin
			Result := 1;
			exit;
		end else
		if verA[i] > verB[i] then
		begin
			Result := -1;
			exit
		end;

	if GetArrayLength(verA) < GetArrayLength(verB) then
	begin
		Result := 1;
		exit;
	end else
	if GetArrayLength(verA) > GetArrayLength(verB) then
	begin
		Result := -1;
		exit;
	end;

	Result := 0;

end;


function Utf82String(const pInput: AnsiString): String;
#ifndef UNICODE
	#error "Unicode Inno Setup required"
#endif
var Output: String;
	ret, outLen, nulPos: Integer;
begin
	outLen := MultiByteToWideChar(CP_UTF8, 0, pInput, -1, Output, 0);
	Output := StringOfChar(#0,outLen);
	ret := MultiByteToWideChar(CP_UTF8, 0, pInput, -1, Output, outLen);

	if ret = 0 then
		RaiseException('MultiByteToWideChar failed: ' + IntToStr(GetLastError));

	nulPos := Pos(#0,Output) - 1;
	if nulPos = -1 then
		nulPos := Length(Output);

    Result := Copy(Output,1,nulPos);
end;


function LoadStringFromUTF8File(const pFileName: String; var oS: String): Boolean;
var Utf8String: AnsiString;
begin
	Result := LoadStringFromFile(pFileName, Utf8String);
	oS := Utf82String(Utf8String);
end;


function String2Utf8(const pInput: String): AnsiString;
var Output: AnsiString;
	ret, outLen, nulPos: Integer;
begin
	outLen := WideCharToMultiByte(CP_UTF8, 0, pInput, -1, Output, 0, 0, 0);
	Output := StringOfChar(#0,outLen);
	ret := WideCharToMultiByte(CP_UTF8, 0, pInput, -1, Output, outLen, 0, 0);

	if ret = 0 then
		RaiseException('WideCharToMultiByte failed: ' + IntToStr(GetLastError));

	nulPos := Pos(#0,Output) - 1;
	if nulPos = -1 then
		nulPos := Length(Output);

    Result := Copy(Output,1,nulPos);
end;


function SaveStringToUTF8File(const pFileName, pS: String; const pAppend: Boolean): Boolean;
begin
	Result := SaveStringToFile(pFileName, String2Utf8(pS), pAppend);
end;


procedure SaveToUninstInf(const pText: AnsiString);
var sUnInf: String;
	sOldContent: String;
begin
	sUnInf := ExpandConstant('{app}\uninst\uninst.inf');

	if not FileExists(sUnInf) then //save small header
		SaveStringToUTF8File(sUnInf,#$feff+'#Additional uninstall tasks'#13#10+ //#$feff BOM is required for LoadStringsFromFile
		                            '#This file uses UTF-8 encoding'#13#10+
		                            '#'#13#10+
		                            '#Empty lines and lines beginning with # are ignored'#13#10+
		                            '#'#13#10+
		                            '#Add uninstallers for GIMP add-ons that should be removed together with GIMP like this:'#13#10+
		                            '#Run:<description>/<full path to uninstaller>/<parameters for automatic uninstall>'#13#10+
		                            '#'#13#10+
		                            '#The file is parsed in reverse order' + #13#10 +
		                            '' + #13#10 //needs '' in front, otherwise preprocessor complains
		                            ,False)
	else
	begin
		if LoadStringFromUTF8File(sUnInf,sOldContent) then
			if Pos(#13#10+pText+#13#10,sOldContent) > 0 then //don't write duplicate lines
				exit;
	end;

	SaveStringToUTF8File(sUnInf,pText+#13#10,True);
end;


procedure GetGimpInfo(const pRootKey: Integer; const pRegPath: String);
var p: Integer;
begin
	If not RegQueryStringValue(pRootKey, pRegPath, RegKey, GimpPath) then //find Gimp install dir
	begin      
		GimpPath := ExpandConstant('{pf}\GIMP 2');
	end;

	If not RegQueryStringValue(pRootKey, pRegPath, VerKey, GimpVer) then //find Gimp version
	begin
		GimpVer := '0';
	end;

	if GimpVer <> '' then //GimpVer at this point contains 'GIMP x.y.z' - strip the first part
	begin
		p := Pos('gimp ',LowerCase(GimpVer));
		if p > 0 then
			GimpVer := Copy(GimpVer,p+5,Length(GimpVer))
		else
			GimpVer := '0';
	end;
end;


procedure WriteUninstallInfo();
begin
	if InstallType <> itOld then //new installer expects components to be listed in the uninst.inf file
		SaveToUninstInf('Run:GIMP Help/'+ExpandConstant('{uninstallexe}')+'//SILENT /NORESTART');
end;


function InitializeSetup(): Boolean;
begin
	Result := True;

	if IsWin64 and RegValueExists(HKLM64, RegPathNew, RegKey) then //check for 64bit GIMP with new installer first
	begin
		GetGimpInfo(HKLM64, RegPathNew);
		InstallType := itNew64;
	end else
	if RegValueExists(HKLM, RegPathNew, RegKey) then //then for 32bit GIMP with new installer
	begin
		GetGimpInfo(HKLM, RegPathNew);
		InstallType := itNew;
	end else
	if RegValueExists(HKLM, RegPath, RegKey) then //and finally for old installer
	begin
		GetGimpInfo(HKLM, RegPath);
		InstallType := itOld;
	end else
	begin
		GimpPath := ExpandConstant('{pf}\GIMP 2'); //provide some defaults
		GimpVer := '0';
	end;
end;


procedure CurStepChanged(pCurStep: TSetupStep);
begin
	case pCurStep of
		ssPostInstall:
		begin
			WriteUninstallInfo();
		end;
	end;
end;


procedure InitializeWizard();
var r: Integer;
begin
	r := SHAutoComplete(WizardForm.DirEdit.Handle,SHACF_FILESYSTEM);
end;


function GetGimpDir(S: String): String;
begin
	Result := GimpPath;
end;


function GetUninstallDir(default: String): String;
begin
	if CompareVersion(GimpVer,'2.4.0') >= 0 then
		Result := ExpandConstant('{app}')
	else
	begin
		if InstallType = itOld then
			Result := ExpandConstant('{app}\setup')
		else
			Result := ExpandConstant('{app}\uninst');
	end;
end;


function GetAppId(default: String): String;
begin
	if InstallType = itOld then
		Result := 'WinGimp-2.0'
	else
		Result := '{#APP_ID}';
end;


(*
function NextButtonClick(CurPageID: Integer): Boolean;
begin
	Result := True;

	case CurPageID of
	wpSelectDir:
		begin
			//you can do checks here
			if not FileExists(AddBackslash(WizardDirValue) + 'bin\gimp-2.6.exe') then
			begin
				if SuppressibleMsgBox(CustomMessage('DirNotGimp'),mbConfirmation,MB_YESNO,IDYES) = IDNO then
					Result := False;
			end;
		end;
	end;
end;
*)

//#expr SaveToFile(AddBackslash(SourcePath) + "Preprocessed.iss")
