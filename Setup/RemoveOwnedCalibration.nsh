  InitPluginsDir
  Push $0
  Push $OUTDIR
  SetOutPath "$PLUGINSDIR"
  File /oname=RemoveOwnedFiles.ps1 "RemoveOwnedFiles.ps1"
  File /oname=OwnedCalibrationManifest.json "OwnedCalibrationManifest.json"
  nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\RemoveOwnedFiles.ps1" -Root "$INSTDIR\HeadphoneCalibrations" -ManifestPath "$PLUGINSDIR\OwnedCalibrationManifest.json" -OwnershipPath "$INSTDIR\.eqapo-owned-calibration.json" -Mode Remove'
  Pop $0
  Pop $OUTDIR
  SetOutPath "$OUTDIR"
  Pop $0
