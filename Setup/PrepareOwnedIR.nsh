  InitPluginsDir
  Push $0
  Push $OUTDIR
  SetOutPath "$PLUGINSDIR"
  File /oname=RemoveOwnedFiles.ps1 "RemoveOwnedFiles.ps1"
  File /oname=OwnedIRManifest.json "OwnedIRManifest.json"
  nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\RemoveOwnedFiles.ps1" -Root "$INSTDIR\IRs" -ManifestPath "$PLUGINSDIR\OwnedIRManifest.json" -OwnershipPath "$INSTDIR\.eqapo-owned-ir.json" -Mode Prepare'
  Pop $0
  Pop $OUTDIR
  SetOutPath "$OUTDIR"
  Pop $0
