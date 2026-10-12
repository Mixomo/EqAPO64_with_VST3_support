import hashlib,json
from pathlib import Path
root=Path(__file__).resolve().parent.parent
for name,directory in [('IR',root/'IRs'),('Calibration',root/'resources/HeadphoneCalibrations')]:
    entries=[dict(path=str(p.relative_to(directory)),sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sorted(directory.rglob('*')) if p.is_file()]
    manifest=f'Owned{name}Manifest.json'
    (root/'Setup'/manifest).write_text(json.dumps(entries,indent=2),encoding='utf-8')
    leaf='IRs' if name=='IR' else 'HeadphoneCalibrations'
    sep=chr(92)
    ownership='.eqapo-owned-'+name.lower()+'.json'
    for mode in ('Prepare','Remove'):
        command=(f'"$SYSDIR{sep}WindowsPowerShell{sep}v1.0{sep}powershell.exe" -NoProfile -ExecutionPolicy Bypass '
                 f'-File "$PLUGINSDIR{sep}RemoveOwnedFiles.ps1" -Root "$INSTDIR{sep}{leaf}" '
                 f'-ManifestPath "$PLUGINSDIR{sep}{manifest}" -OwnershipPath "$INSTDIR{sep}{ownership}" -Mode {mode}')
        lines=['  InitPluginsDir','  Push $0','  Push $OUTDIR','  SetOutPath "$PLUGINSDIR"',
               '  File /oname=RemoveOwnedFiles.ps1 "RemoveOwnedFiles.ps1"',
               f'  File /oname={manifest} "{manifest}"',
               f"  nsExec::ExecToLog '{command}'",'  Pop $0','  Pop $OUTDIR','  SetOutPath "$OUTDIR"','  Pop $0']
        (root/'Setup'/f'{mode}Owned{name}.nsh').write_text(chr(10).join(lines)+chr(10),encoding='utf-8')
    print(name,len(entries))
