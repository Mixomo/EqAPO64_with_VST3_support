!include "LogicLib.nsh"
!include "MUI2.nsh"
!include "WinVer.nsh"
!include "x64.nsh"

Unicode true
ManifestDPIAware true
;Use more efficient compression
SetCompressor /SOLID lzma

!searchparse /file ..\version.h `#define MAJOR ` MAJOR
!searchparse /file ..\version.h `#define MINOR ` MINOR
!searchparse /file ..\version.h `#define REVISION ` REVISION
!if ${REVISION} == 0
!define VERSION ${MAJOR}.${MINOR}
!else
!define VERSION ${MAJOR}.${MINOR}.${REVISION}
!endif

!define REGPATH "Software\EqualizerAPO"
!define UNINST_REGPATH "Software\Microsoft\Windows\CurrentVersion\Uninstall\EqualizerAPO"

;--------------------------------
;General

  ;Name and file
  Name "Equalizer APO ${VERSION}"

  ;Request application privileges for Windows Vista
  RequestExecutionLevel admin
  
;--------------------------------
;Variables

  Var StartMenuFolder
  Var OldStartMenuFolder
  Var OLDINSTDIR
  Var InstallLogPath
  Var DiagnosticIssues
  
;--------------------------------
;Interface Settings

  !define MUI_ABORTWARNING
  !define MUI_COMPONENTSPAGE_NODESC
  !define MUI_WELCOMEPAGE_TITLE_3LINES
  !define MUI_LANGDLL_REGISTRY_ROOT "HKLM"
  !define MUI_LANGDLL_REGISTRY_KEY ${REGPATH}
  !define MUI_LANGDLL_REGISTRY_VALUENAME "Installer Language"

;--------------------------------
;Pages

  !insertmacro MUI_PAGE_WELCOME
  !insertmacro MUI_PAGE_LICENSE ..\LICENSE
  !insertmacro MUI_PAGE_DIRECTORY
  
;Start Menu Folder Page Configuration
  !define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKLM" 
  !define MUI_STARTMENUPAGE_REGISTRY_KEY ${REGPATH} 
  !define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"
  
  !insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
  !insertmacro MUI_PAGE_COMPONENTS
  !insertmacro MUI_PAGE_INSTFILES
  !insertmacro MUI_PAGE_FINISH

  !insertmacro MUI_UNPAGE_WELCOME
  !insertmacro MUI_UNPAGE_CONFIRM
  !insertmacro MUI_UNPAGE_COMPONENTS
  !insertmacro MUI_UNPAGE_INSTFILES
  !insertmacro MUI_UNPAGE_FINISH

;--------------------------------
;Languages

  !insertmacro MUI_LANGUAGE "English"
  !insertmacro MUI_LANGUAGE "Spanish"
  !insertmacro MUI_LANGUAGE "German"

;--------------------------------
;Macros
Var renamePath
Var renameIndex
!macro RenameAndDelete path
  ${If} ${FileExists} "${path}"
    StrCpy $renamePath "${path}.old"
    StrCpy $renameIndex "0"
    ${While} ${FileExists} "$renamePath"
      StrCpy $renamePath "${path}.old.$renameIndex"
      IntOp $renameIndex $renameIndex + 1
    ${EndWhile}
    Rename "${path}" "$renamePath"
  ${EndIf}
!macroend

!macro LogLoadLibrary dll
  FileWrite $9 "Checking ${dll}... "
  ${IfNot} ${FileExists} "$INSTDIR\${dll}"
    FileWrite $9 "missing$\r$\n"
    StrCpy $DiagnosticIssues "$DiagnosticIssues- ${dll}: missing$\r$\n"
  ${Else}
    FileWrite $9 "present$\r$\n"
  ${EndIf}
!macroend

!macro LogFileExists file
  FileWrite $9 "Checking ${file}... "
  ${IfNot} ${FileExists} "$INSTDIR\${file}"
    FileWrite $9 "missing$\r$\n"
    StrCpy $DiagnosticIssues "$DiagnosticIssues- ${file}: missing$\r$\n"
  ${Else}
    FileWrite $9 "present$\r$\n"
  ${EndIf}
!macroend
    
LangString VersionError ${LANG_ENGLISH} "This installer is only supposed to be run on {0} Windows. Please use the {1} installer."
LangString VersionError ${LANG_SPANISH} "Este instalador solo debe ejecutarse en Windows {0}. Use el instalador {1}."
LangString VersionError ${LANG_GERMAN} "Dieses Installationsprogramm kann nur auf einem {0}-Windows verwendet werden. Bitte nutzen Sie die {1}-Version."

LangString UCRTError ${LANG_ENGLISH} "Your Windows installation is missing required updates to use this program. Please install remaining Windows updates or the Visual C++ Redistributable for Visual Studio 2015 - 2022.$\n$\nDo you want to download the Visual C++ Redistributable now?"
LangString UCRTError ${LANG_SPANISH} "A su instalacion de Windows le faltan actualizaciones necesarias para usar este programa. Instale las actualizaciones pendientes de Windows o Visual C++ Redistributable para Visual Studio 2015 - 2022.$\n$\nDesea descargar Visual C++ Redistributable ahora?"
LangString UCRTError ${LANG_GERMAN} "Ihrer Windows-Installation fehlen ben�tigte Updates, um dieses Programm zu verwenden. Bitte installieren Sie ausstehende Windows-Updates oder das Visual C++ Redistributable f�r Visual Studio 2015 - 2022.$\n$\nM�chten Sie jetzt das Visual C++ Redistributable herunterladen?"
LangString CloseAppsPrompt ${LANG_ENGLISH} "Setup can close running Equalizer APO applications before installing. Unsaved configuration editor changes may be lost.$\n$\nDo you want setup to close them now?"
LangString CloseAppsPrompt ${LANG_SPANISH} "El instalador puede cerrar aplicaciones de Equalizer APO antes de instalar. Los cambios no guardados del editor de configuracion pueden perderse.$\n$\nDesea que el instalador las cierre ahora?"
LangString CloseAppsPrompt ${LANG_GERMAN} "Das Setup kann laufende Equalizer APO-Anwendungen vor der Installation schlie�en. Nicht gespeicherte �nderungen im Konfigurationseditor k�nnen verloren gehen.$\n$\nSollen sie jetzt geschlossen werden?"
LangString RestorePointWarning ${LANG_ENGLISH} "Setup could not create a Windows restore point.$\n$\nThis can happen when System Protection is disabled, or when a restore point already exists from the last 24 hours. By default, Windows policy may block creating more than one restore point within the same 24-hour period.$\n$\nInstallation will continue."
LangString RestorePointWarning ${LANG_SPANISH} "El instalador no pudo crear un punto de restauracion de Windows.$\n$\nEsto puede ocurrir si Proteccion del sistema esta desactivada, o si ya existe un punto de restauracion creado en las ultimas 24 horas. De forma predeterminada, las politicas de Windows pueden bloquear la creacion de mas de un punto de restauracion dentro del mismo periodo de 24 horas.$\n$\nLa instalacion continuara."
LangString RestorePointWarning ${LANG_GERMAN} "Das Setup konnte keinen Windows-Wiederherstellungspunkt erstellen.$\n$\nDies kann passieren, wenn der Computerschutz deaktiviert ist oder wenn bereits ein Wiederherstellungspunkt aus den letzten 24 Stunden existiert. Standardm��ig kann Windows verhindern, dass innerhalb desselben 24-Stunden-Zeitraums mehr als ein Wiederherstellungspunkt erstellt wird.$\n$\nDie Installation wird fortgesetzt."

;--------------------------------
;Functions
Function .onInit
  !if ${LIBPATH} != "lib32"
    SetRegView 64
  !endif
  !insertmacro MUI_LANGDLL_DISPLAY
  ;Get installation folder from registry if available
  ReadRegStr $INSTDIR HKLM ${REGPATH} "InstallPath"

  ;Use default installation folder otherwise
  ${If} $INSTDIR == ""
    StrCpy $INSTDIR "$PROGRAMFILES64\EqualizerAPO"
  ${EndIf}
    
  !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
  ${If} ${IsNativeIA32}
    StrCpy $0 "x86"
  ${ElseIf} ${IsNativeAMD64}
    StrCpy $0 "x64"
  ${ElseIf} ${IsNativeARM64}
    StrCpy $0 "ARM64"
  ${EndIf}
  
  ${If} $0 != ${TARGET_ARCH}
    MessageBox MB_OK|MB_ICONSTOP "This installer is only supposed to be run on ${TARGET_ARCH} Windows. Please use the $0 installer."
    Abort
  ${EndIf}
  
  ${IfNot} ${AtLeastWin10}
    System::Call 'KERNEL32::LoadLibrary(t "ucrtbase.dll")p.r0'
    ${If} $0 P= 0
      MessageBox MB_YESNO|MB_ICONSTOP $(UCRTError) IDNO skipDownload
      ExecShell "open" "${VCREDIST_URL}"
      skipDownload:
      Abort
    ${EndIf}
  ${EndIf}
FunctionEnd

Function CloseRunningApplications
  ${If} ${FileExists} "$INSTDIR"
    MessageBox MB_YESNO|MB_ICONQUESTION $(CloseAppsPrompt) IDNO done
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM Editor.exe /T /F'
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM DeviceSelector.exe /T /F'
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM UpdateChecker.exe /T /F'
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM Benchmark.exe /T /F'
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM VoicemeeterClient.exe /T /F'
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM EqApoOutProcHost.exe /T /F'
  ${EndIf}
  done:
FunctionEnd

Function CreateRestorePoint
  DetailPrint "Creating Windows restore point..."
  StrCpy $0 "$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
  ${IfNot} ${FileExists} "$0"
    StrCpy $0 "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe"
  ${EndIf}

  ${If} ${FileExists} "$0"
    nsExec::ExecToLog '"$0" -NoProfile -ExecutionPolicy Bypass -Command "try { Checkpoint-Computer -Description EqualizerAPO_${VERSION}_PreInstall -RestorePointType APPLICATION_INSTALL -ErrorAction Stop; exit 0 } catch { exit 1 }"'
    Pop $1
    ${If} $1 == 0
      DetailPrint "Windows restore point created."
    ${Else}
      DetailPrint "Windows restore point was not created. PowerShell exit code: $1"
      MessageBox MB_ICONEXCLAMATION|MB_OK $(RestorePointWarning)
    ${EndIf}
  ${Else}
    DetailPrint "PowerShell was not found. Skipping restore point creation."
    MessageBox MB_ICONEXCLAMATION|MB_OK $(RestorePointWarning)
  ${EndIf}
FunctionEnd

Function WriteInstallDiagnostics
  StrCpy $DiagnosticIssues ""
  StrCpy $InstallLogPath "$INSTDIR\install-diagnostics.log"
  FileOpen $9 "$InstallLogPath" w
  ${If} $9 == ""
    Return
  ${EndIf}

  FileWrite $9 "Equalizer APO installer diagnostics$\r$\n"
  FileWrite $9 "Version: ${VERSION}$\r$\n"
  FileWrite $9 "Target architecture: ${TARGET_ARCH}$\r$\n"
  FileWrite $9 "Install path: $INSTDIR$\r$\n"
  FileWrite $9 "$\r$\n"

  !insertmacro LogLoadLibrary "EqualizerAPO.dll"
  !insertmacro LogFileExists "EqApoOutProcHost.exe"
  !insertmacro LogFileExists "Editor.exe"
  !insertmacro LogFileExists "DeviceSelector.exe"
  !insertmacro LogFileExists "Benchmark.exe"
  !insertmacro LogFileExists "VoicemeeterClient.exe"
  !insertmacro LogFileExists "UpdateChecker.exe"
  !insertmacro LogLoadLibrary "libfftw3.dll"
  !insertmacro LogLoadLibrary "fftw3.dll"
  !insertmacro LogLoadLibrary "sndfile.dll"
  !insertmacro LogLoadLibrary "FLAC.dll"
  !insertmacro LogLoadLibrary "libmp3lame.dll"
  !insertmacro LogLoadLibrary "mpg123.dll"
  !insertmacro LogLoadLibrary "ogg.dll"
  !insertmacro LogLoadLibrary "opus.dll"
  !insertmacro LogLoadLibrary "vorbis.dll"
  !insertmacro LogLoadLibrary "vorbisenc.dll"
  !insertmacro LogLoadLibrary "vorbisfile.dll"
  !insertmacro LogLoadLibrary "msvcp140.dll"
  !insertmacro LogLoadLibrary "msvcp140_1.dll"
  !insertmacro LogLoadLibrary "vcruntime140.dll"
  !insertmacro LogLoadLibrary "vcruntime140_1.dll"
  !insertmacro LogLoadLibrary "Qt6Core.dll"
  !insertmacro LogLoadLibrary "Qt6Gui.dll"
  !insertmacro LogLoadLibrary "Qt6Network.dll"
  !insertmacro LogLoadLibrary "Qt6Svg.dll"
  !insertmacro LogLoadLibrary "Qt6Widgets.dll"
  !insertmacro LogLoadLibrary "d3dcompiler_47.dll"

  FileClose $9
FunctionEnd

Function InstallBundledRuntimeDlls
  SetOutPath "$INSTDIR"
  File "${LIBPATH}\libfftw3.dll"
  File "${LIBPATH}\fftw3.dll"
  File "${LIBPATH}\sndfile.dll"
  File "${LIBPATH}\FLAC.dll"
  File "${LIBPATH}\libmp3lame.dll"
  File "${LIBPATH}\mpg123.dll"
  File "${LIBPATH}\ogg.dll"
  File "${LIBPATH}\opus.dll"
  File "${LIBPATH}\vorbis.dll"
  File "${LIBPATH}\vorbisenc.dll"
  File "${LIBPATH}\vorbisfile.dll"
  File "${LIBPATH}\msvcp140.dll"
  File "${LIBPATH}\msvcp140_1.dll"
  File "${LIBPATH}\vcruntime140.dll"
  File "${LIBPATH}\vcruntime140_1.dll"
  File "${LIBPATH}\d3dcompiler_47.dll"
  !if /FileExists "${LIBPATH}\dxcompiler.dll"
    File "${LIBPATH}\dxcompiler.dll"
  !endif
  !if /FileExists "${LIBPATH}\dxil.dll"
    File "${LIBPATH}\dxil.dll"
  !endif
  File "${LIBPATH}\icuuc.dll"
  File "${LIBPATH}\Qt6Core.dll"
  File "${LIBPATH}\Qt6Gui.dll"
  File "${LIBPATH}\Qt6Network.dll"
  File "${LIBPATH}\Qt6Svg.dll"
  File "${LIBPATH}\Qt6Widgets.dll"
FunctionEnd

Function WriteX64LoadDiagnostics
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File "x64-load-check.ps1"

  StrCpy $0 "$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
  ${IfNot} ${FileExists} "$0"
    StrCpy $0 "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe"
  ${EndIf}

  ${If} ${FileExists} "$0"
    nsExec::ExecToLog '"$0" -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\x64-load-check.ps1" "$INSTDIR\EqualizerAPO.dll" "$InstallLogPath"'
    Pop $2
    FileOpen $9 "$InstallLogPath" a
    ${If} $9 != ""
      FileWrite $9 "64-bit LoadLibrary diagnostic exit code: $2$\r$\n"
      FileClose $9
    ${EndIf}
  ${Else}
    FileOpen $9 "$InstallLogPath" a
    ${If} $9 != ""
      FileWrite $9 "$\r$\n64-bit PowerShell was not found. Skipping LoadLibrary diagnostic.$\r$\n"
      FileClose $9
    ${EndIf}
  ${EndIf}
FunctionEnd

;--------------------------------
;Installer Sections
LangString SecCheckForUpdates ${LANG_ENGLISH} "Check for updates automatically"
LangString SecCheckForUpdates ${LANG_SPANISH} "Buscar actualizaciones automaticamente"
LangString SecCheckForUpdates ${LANG_GERMAN} "Automatisch auf Updates pr�fen"

Section $(SecCheckForUpdates) SecCheckForUpdates
SectionEnd

Section "-Install"
  SetOutPath "$INSTDIR"
  Call CreateRestorePoint
  Call CloseRunningApplications

  ;Possibly remove files from previous installation
  !insertmacro MUI_STARTMENU_GETFOLDER Application $OldStartMenuFolder
  RMDir /r "$SMPROGRAMS\$OldStartMenuFolder"
  
  Delete "$INSTDIR\Configurator.exe"
  Delete "$INSTDIR\Qt5Core.dll"
  Delete "$INSTDIR\Qt5Gui.dll"
  Delete "$INSTDIR\Qt5Widgets.dll"
  Delete "$INSTDIR\qt\imageformats\qgif.dll"
  Delete "$INSTDIR\qt\imageformats\qjpeg.dll"
  Delete "$INSTDIR\qt\styles\qwindowsvistastyle.dll"
  
  ;Rename before delete as these files may be in use
  !insertmacro RenameAndDelete "$INSTDIR\EqualizerAPO.dll"
  !insertmacro RenameAndDelete "$INSTDIR\EqApoOutProcHost.exe"
  !insertmacro RenameAndDelete "$INSTDIR\EqApoJuceVST3Host.dll"
  !insertmacro RenameAndDelete "$INSTDIR\EqApoOutProcJuceHost.exe"
  !insertmacro RenameAndDelete "$INSTDIR\libfftw3-3.dll"
  !insertmacro RenameAndDelete "$INSTDIR\libfftw3.dll"
  !insertmacro RenameAndDelete "$INSTDIR\fftw3.dll"
  !insertmacro RenameAndDelete "$INSTDIR\libsndfile-1.dll"
  !insertmacro RenameAndDelete "$INSTDIR\sndfile.dll"
  !insertmacro RenameAndDelete "$INSTDIR\samplerate.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcp100.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcr100.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcp120.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcr120.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcp140.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcp140_1.dll"
  !insertmacro RenameAndDelete "$INSTDIR\msvcp140_2.dll"
  !insertmacro RenameAndDelete "$INSTDIR\icudt.dll"
  !insertmacro RenameAndDelete "$INSTDIR\icuin.dll"
  !insertmacro RenameAndDelete "$INSTDIR\icuuc.dll"
  !insertmacro RenameAndDelete "$INSTDIR\icudt78.dll"
  !insertmacro RenameAndDelete "$INSTDIR\icuin78.dll"
  !insertmacro RenameAndDelete "$INSTDIR\icuuc78.dll"
  !insertmacro RenameAndDelete "$INSTDIR\VoicemeeterClient.exe"
  !insertmacro RenameAndDelete "$INSTDIR\vcruntime140.dll"
  !insertmacro RenameAndDelete "$INSTDIR\vcruntime140_1.dll"
  !insertmacro RenameAndDelete "$INSTDIR\FLAC.dll"
  !insertmacro RenameAndDelete "$INSTDIR\libmp3lame.dll"
  !insertmacro RenameAndDelete "$INSTDIR\mpg123.dll"
  !insertmacro RenameAndDelete "$INSTDIR\ogg.dll"
  !insertmacro RenameAndDelete "$INSTDIR\opus.dll"
  !insertmacro RenameAndDelete "$INSTDIR\vorbis.dll"
  !insertmacro RenameAndDelete "$INSTDIR\vorbisenc.dll"
  !insertmacro RenameAndDelete "$INSTDIR\vorbisfile.dll"
  !insertmacro RenameAndDelete "$INSTDIR\d3dcompiler_47.dll"
  !insertmacro RenameAndDelete "$INSTDIR\dxcompiler.dll"
  !insertmacro RenameAndDelete "$INSTDIR\dxil.dll"
  !insertmacro RenameAndDelete "$INSTDIR\Qt6Core.dll"
  !insertmacro RenameAndDelete "$INSTDIR\Qt6Gui.dll"
  !insertmacro RenameAndDelete "$INSTDIR\Qt6Network.dll"
  !insertmacro RenameAndDelete "$INSTDIR\Qt6Svg.dll"
  !insertmacro RenameAndDelete "$INSTDIR\Qt6Widgets.dll"
  
  File "${BINPATH}\EqualizerAPO.dll"
  File "${BINPATH}\EqApoOutProcHost.exe"
  File "${BINPATH}\DeviceSelector.exe"
  File "${BINPATH}\Benchmark.exe"
  File "${BINPATH}\VoicemeeterClient.exe"
  File "${BINPATH}\UpdateChecker.exe"
  File "${BINPATH_EDITOR}\Editor.exe"
  
  File "${LIBPATH}\libfftw3.dll"
  File "${LIBPATH}\fftw3.dll"
  File "${LIBPATH}\sndfile.dll"
  File "${LIBPATH}\FLAC.dll"
  File "${LIBPATH}\libmp3lame.dll"
  File "${LIBPATH}\mpg123.dll"
  File "${LIBPATH}\ogg.dll"
  File "${LIBPATH}\opus.dll"
  File "${LIBPATH}\vorbis.dll"
  File "${LIBPATH}\vorbisenc.dll"
  File "${LIBPATH}\vorbisfile.dll"
  File "${LIBPATH}\msvcp140.dll"
  File "${LIBPATH}\msvcp140_1.dll"
  File "${LIBPATH}\vcruntime140.dll"
  File "${LIBPATH}\vcruntime140_1.dll"
  File "${LIBPATH}\d3dcompiler_47.dll"
  !if /FileExists "${LIBPATH}\dxcompiler.dll"
    File "${LIBPATH}\dxcompiler.dll"
  !endif
  !if /FileExists "${LIBPATH}\dxil.dll"
    File "${LIBPATH}\dxil.dll"
  !endif
  File "${LIBPATH}\icuuc.dll"
  File "${LIBPATH}\Qt6Core.dll"
  File "${LIBPATH}\Qt6Gui.dll"
  File "${LIBPATH}\Qt6Network.dll"
  File "${LIBPATH}\Qt6Svg.dll"
  File "${LIBPATH}\Qt6Widgets.dll"
  
  CreateDirectory "$INSTDIR\qt"
  CreateDirectory "$INSTDIR\qt\generic"
  CreateDirectory "$INSTDIR\qt\iconengines"
  CreateDirectory "$INSTDIR\qt\imageformats"
  CreateDirectory "$INSTDIR\qt\networkinformation"
  CreateDirectory "$INSTDIR\qt\platforms"
  CreateDirectory "$INSTDIR\qt\styles"
  CreateDirectory "$INSTDIR\qt\tls"

  File /oname=qt\generic\qtuiotouchplugin.dll "${LIBPATH}\qt\generic\qtuiotouchplugin.dll"
  File /oname=qt\iconengines\qsvgicon.dll "${LIBPATH}\qt\iconengines\qsvgicon.dll"
  File /nonfatal /oname=qt\imageformats\qgif.dll "${LIBPATH}\qt\imageformats\qgif.dll"
  File /oname=qt\imageformats\qico.dll "${LIBPATH}\qt\imageformats\qico.dll"
  File /nonfatal /oname=qt\imageformats\qjpeg.dll "${LIBPATH}\qt\imageformats\qjpeg.dll"
  File /oname=qt\imageformats\qsvg.dll "${LIBPATH}\qt\imageformats\qsvg.dll"
  File /oname=qt\networkinformation\qnetworklistmanager.dll "${LIBPATH}\qt\networkinformation\qnetworklistmanager.dll"
  File /oname=qt\platforms\qwindows.dll "${LIBPATH}\qt\platforms\qwindows.dll"
  File /oname=qt\styles\qmodernwindowsstyle.dll "${LIBPATH}\qt\styles\qmodernwindowsstyle.dll"
  File /oname=qt\tls\qcertonlybackend.dll "${LIBPATH}\qt\tls\qcertonlybackend.dll"
  File /oname=qt\tls\qschannelbackend.dll "${LIBPATH}\qt\tls\qschannelbackend.dll"

  CreateDirectory "$INSTDIR\HeadphoneCalibrations"
  SetOutPath "$INSTDIR\HeadphoneCalibrations"
  File /r "${LIBPATH}\HeadphoneCalibrations\*"
  SetOutPath "$INSTDIR"

  CreateDirectory "$INSTDIR\IRs"
  SetOutPath "$INSTDIR\IRs"
  File /nonfatal /r "${LIBPATH}\IRs\*"
  SetOutPath "$INSTDIR"

  File "Configuration tutorial (online).url"
  File "Configuration reference (online).url"
  
  CreateDirectory "$INSTDIR\config"
  CreateDirectory "$INSTDIR\VSTPlugins"
  
  SetOverwrite off
  File /oname=config\config.txt "config\config.txt"
  File /oname=config\example.txt "config\example.txt"
  File /oname=config\demo.txt "config\demo.txt"
  File /oname=config\multichannel.txt "config\multichannel.txt"
  File /oname=config\iir_lowpass.txt "config\iir_lowpass.txt"
  File /oname=config\selective_delay.txt "config\selective_delay.txt"
  SetOverwrite on

  ;Grant write access to the config directory for all users
  nsExec::ExecToLog '"$SYSDIR\icacls.exe" "$INSTDIR\config" /grant *S-1-5-32-545:(OI)(CI)F /T /C'

  ReadRegStr $OLDINSTDIR HKLM ${REGPATH} "InstallPath"
  WriteRegStr HKLM ${REGPATH} "InstallPath" "$INSTDIR"
  
  ;Write ConfigPath if non-existing or if InstallPath has changed
  ReadRegStr $0 HKLM ${REGPATH} "ConfigPath"
  ${If} $0 == ""
  ${OrIf} $INSTDIR != $OLDINSTDIR
	WriteRegStr HKLM ${REGPATH} "ConfigPath" "$INSTDIR\config"
  ${EndIf}
	
  ReadRegStr $0 HKLM ${REGPATH} "EnableTrace"
  ${If} $0 == ""
	WriteRegStr HKLM ${REGPATH} "EnableTrace" "false"
  ${EndIf}

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
  ;Create shortcuts
  CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
  CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Equalizer APO Configuration Editor.lnk" "$INSTDIR\Editor.exe"
  CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Configuration tutorial (online).lnk" "$INSTDIR\Configuration tutorial (online).url"
  CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Configuration reference (online).lnk" "$INSTDIR\Configuration reference (online).url"
  CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Equalizer APO Device Selector.lnk" "$INSTDIR\DeviceSelector.exe"
  CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Benchmark.lnk" "$INSTDIR\Benchmark.exe"
  CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  !insertmacro MUI_STARTMENU_WRITE_END
  
  WriteRegStr HKLM ${UNINST_REGPATH} "DisplayName" "Equalizer APO"
  WriteRegStr HKLM ${UNINST_REGPATH} "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM ${UNINST_REGPATH} "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM ${UNINST_REGPATH} "NoModify" 1
  WriteRegDWORD HKLM ${UNINST_REGPATH} "NoRepair" 1

  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Audio" "DisableProtectedAudioDG" 1
  Call WriteInstallDiagnostics
  ;RegDLL doesn't work for 64 bit dlls
  ExecWait '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\EqualizerAPO.dll"' $1
  ${If} $1 != 0
    DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Audio" "DisableProtectedAudioDG"
    FileOpen $9 "$InstallLogPath" a
    ${If} $9 != ""
      FileWrite $9 "$\r$\nregsvr32 exit code: $1$\r$\n"
      FileClose $9
    ${EndIf}
    Call WriteX64LoadDiagnostics
    ${If} $DiagnosticIssues != ""
      MessageBox MB_ICONQUESTION|MB_YESNO "Equalizer APO could not be registered.$\r$\n$\r$\nThe installer found these missing or incompatible bundled files:$\r$\n$\r$\n$DiagnosticIssues$\r$\nSetup can reinstall the bundled DLLs into the Equalizer APO folder and retry registration.$\r$\n$\r$\nThis will not replace Windows system DLLs.$\r$\n$\r$\nDo you want to reinstall the bundled DLLs and retry?" IDNO registrationFailed
      Call InstallBundledRuntimeDlls
      Call WriteInstallDiagnostics
      ExecWait '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\EqualizerAPO.dll"' $1
      ${If} $1 == 0
        WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Audio" "DisableProtectedAudioDG" 1
        Goto registrationOk
      ${EndIf}
      DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Audio" "DisableProtectedAudioDG"
      FileOpen $9 "$InstallLogPath" a
      ${If} $9 != ""
        FileWrite $9 "$\r$\nregsvr32 retry exit code: $1$\r$\n"
        FileClose $9
      ${EndIf}
      Call WriteX64LoadDiagnostics
    ${EndIf}
    registrationFailed:
      MessageBox MB_ICONSTOP|MB_OK "Equalizer APO could not be registered. Installation will stop before modifying audio devices.$\r$\n$\r$\nThis usually means a required runtime DLL is missing or incompatible.$\r$\n$\r$\nregsvr32 exit code: $1$\r$\nDiagnostics log: $InstallLogPath"
      Abort
  ${EndIf}
  registrationOk:

  ExecWait '"$INSTDIR\DeviceSelector.exe" /i' $0
  
  ${If} ${SectionIsSelected} ${SecCheckForUpdates}
    ExecWait '"$INSTDIR\UpdateChecker.exe" -i'
  ${Else}
    ExecWait '"$INSTDIR\UpdateChecker.exe" -u'
  ${EndIf}

  ;Hopefully, the renamed files can be deleted without reboot after the Device Selector has restarted the audio service
  Delete /REBOOTOK "$INSTDIR\*.old"
  Delete /REBOOTOK "$INSTDIR\*.old.*"
  
  ${If} $0 == "0"
    SetRebootFlag false
  ${Else}
    SetRebootFlag true
  ${EndIf}
  
SectionEnd

;--------------------------------
;Uninstaller Sections

LangString SecRemoveName ${LANG_ENGLISH} "Remove configurations and registry backups"
LangString SecRemoveName ${LANG_SPANISH} "Eliminar configuraciones y copias de seguridad del registro"
LangString SecRemoveName ${LANG_GERMAN} "Konfigurationen und Registrierungsbackups entfernen"

Section /o un.$(SecRemoveName)
  
  Delete "$INSTDIR\*.reg"
  RMDir /REBOOTOK /r "$INSTDIR\config"
  DeleteRegKey HKCU ${REGPATH}
  
SectionEnd

Section "-un.Uninstall"
  !if ${LIBPATH} != "lib32"
	SetRegView 64
  !endif

  ;Qt applications only work if working directory is set to application directory
  Push $OUTDIR
  SetOutPath $INSTDIR
  ExecWait '"$INSTDIR\UpdateChecker.exe" -u'
  ExecWait '"$INSTDIR\DeviceSelector.exe" /u'
  nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM EqApoOutProcHost.exe /T /F'
  Pop $OUTDIR
  SetOutPath $OUTDIR
  
  ExecWait '"$SYSDIR\regsvr32.exe" /u /s "$INSTDIR\EqualizerAPO.dll"'
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Audio" "DisableProtectedAudioDG"
  
  !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
  RMDir /r "$SMPROGRAMS\$StartMenuFolder"
  
  RMDir "$INSTDIR\VSTPlugins"
  
  Delete "$INSTDIR\Configuration reference (online).url"
  Delete "$INSTDIR\Configuration tutorial (online).url"
  
  RMDir /r "$INSTDIR\qt"
  RMDir /r "$INSTDIR\HeadphoneCalibrations"
  RMDir /r "$INSTDIR\IRs"

  Delete "$INSTDIR\Qt6Widgets.dll"
  Delete "$INSTDIR\Qt6Svg.dll"
  Delete "$INSTDIR\Qt6Network.dll"
  Delete "$INSTDIR\Qt6Gui.dll"
  Delete "$INSTDIR\Qt6Core.dll"
  Delete "$INSTDIR\icuuc.dll"
  Delete "$INSTDIR\dxil.dll"
  Delete "$INSTDIR\dxcompiler.dll"
  Delete "$INSTDIR\d3dcompiler_47.dll"
  Delete "$INSTDIR\vcruntime140_1.dll"
  Delete "$INSTDIR\vcruntime140.dll"
  Delete "$INSTDIR\msvcp140_1.dll"
  Delete "$INSTDIR\msvcp140.dll"
  Delete /REBOOTOK "$INSTDIR\sndfile.dll"
  Delete /REBOOTOK "$INSTDIR\samplerate.dll"
  Delete /REBOOTOK "$INSTDIR\libfftw3.dll"
  Delete /REBOOTOK "$INSTDIR\fftw3.dll"
  Delete "$INSTDIR\Editor.exe"
  
  Delete "$INSTDIR\UpdateChecker.exe"
  Delete "$INSTDIR\VoicemeeterClient.exe"
  Delete "$INSTDIR\Benchmark.exe"
  Delete /REBOOTOK "$INSTDIR\EqApoOutProcHost.exe"
  Delete "$INSTDIR\DeviceSelector.exe"
  Delete /REBOOTOK "$INSTDIR\EqualizerAPO.dll"

  Delete "$INSTDIR\Uninstall.exe"

  ;Only remove if empty
  RMDir /REBOOTOK "$INSTDIR"

  DeleteRegKey HKLM ${UNINST_REGPATH}
  DeleteRegKey /ifempty HKLM ${REGPATH}

SectionEnd
