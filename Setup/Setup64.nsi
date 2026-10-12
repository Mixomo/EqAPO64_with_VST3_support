
!define BINPATH "..\x64\Release"
!define BINPATH_EDITOR "..\x64\Release"
!define LIBPATH "lib64"
!define VCREDIST_URL "https://aka.ms/vs/17/release/vc_redist.x64.exe"
!define TARGET_ARCH "x64"

!include "Setup.nsi"

!ifdef OUTPUT_FILE
OutFile "${OUTPUT_FILE}"
!else
OutFile "EqualizerAPO-x64-1.4.2-with-VST3-support-V3.0.0.exe"
!endif
