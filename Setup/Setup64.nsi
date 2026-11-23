
!define BINPATH "..\x64\Release"
!define BINPATH_EDITOR "..\x64\Release"
!define LIBPATH "lib64"
!define VCREDIST_URL "https://aka.ms/vs/17/release/vc_redist.x64.exe"
!define TARGET_ARCH "x64"

!include "Setup.nsi"

; Release number of this VST3 fork (shown in the installer file name).
!ifndef FORK_VERSION
!define FORK_VERSION "3.0.5"
!endif

!ifdef OUTPUT_FILE
OutFile "${OUTPUT_FILE}"
!else
OutFile "EqualizerAPO-x64-1.4.2-with-VST3-support-V${FORK_VERSION}.exe"
!endif
