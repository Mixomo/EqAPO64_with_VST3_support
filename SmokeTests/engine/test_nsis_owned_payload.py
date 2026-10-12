"""Execute the generated ownership include and verify NSIS stack/output-directory invariants."""
import argparse,subprocess,os,json
from pathlib import Path

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    project=Path(__file__).resolve().parents[2];args.output.mkdir(parents=True,exist_ok=True)
    results=[]
    for name in ('IR','Calibration'):
        leaf='IRs' if name=='IR' else 'HeadphoneCalibrations'
        case=(args.output/name).resolve();case.mkdir(parents=True,exist_ok=True)
        include=(project/'Setup'/f'PrepareOwned{name}.nsh').read_text()
        # Inspect restored OUTDIR before using it; a broken include cannot extract
        # the test payload into an unintended directory.
        include=include.replace('  SetOutPath "$OUTDIR"','''  FileOpen $2 "$INSTDIR\\outdir.txt" w
  FileWrite $2 "$OUTDIR"
  FileClose $2
  StrCmp $OUTDIR "$INSTDIR\\'''+leaf+'''" safe bad
bad:
  SetErrorLevel 7
  Quit
safe:
  SetOutPath "$OUTDIR"''')
        (case/'include.nsh').write_text(include)
        (case/'payload.txt').write_text('regression payload')
        nsi=f'''Unicode true
SilentInstall silent
RequestExecutionLevel user
OutFile "{case / 'test.exe'}"
InstallDir "{case / 'install'}"
Section
 SetOutPath "$INSTDIR\\{leaf}"
 StrCpy $0 2468
 !include "{case / 'include.nsh'}"
 File /oname=payload.txt "{case / 'payload.txt'}"
 FileOpen $2 "$INSTDIR\\caller-variable.txt" w
 FileWrite $2 "$0"
 FileClose $2
SectionEnd
'''
        script=case/'test.nsi';script.write_text(nsi)
        compiled=subprocess.run([str(project/'third_party/nsis-3.11/makensis.exe'),'/NOCD',str(script)],cwd=project/'Setup',capture_output=True,text=True,timeout=60)
        (case/'compile.log').write_text(compiled.stdout+compiled.stderr)
        if compiled.returncode:raise RuntimeError('Compile failed')
        temporary=case/'temp';temporary.mkdir(exist_ok=True)
        env=os.environ.copy();env['TEMP']=env['TMP']=str(temporary)
        run=subprocess.run([str(case/'test.exe'),'/S'],cwd=case,env=env,timeout=30)
        observed=(case/'install/outdir.txt').read_text(encoding='utf-8-sig')
        expected=str(case/'install'/leaf)
        caller=(case/'install/caller-variable.txt').read_text() if (case/'install/caller-variable.txt').exists() else None
        passed=run.returncode==0 and observed.casefold()==expected.casefold() and (case/'install'/leaf/'payload.txt').exists() and caller=='2468'
        results.append(dict(include=f'PrepareOwned{name}.nsh',status='PASS' if passed else 'FAIL',exit_code=run.returncode,observed_outdir=observed,expected_outdir=expected,caller_variable=caller))
    (args.output/'results.json').write_text(json.dumps(results,indent=2))
    print(json.dumps(results),flush=True)
    if any(r['status']!='PASS' for r in results):raise SystemExit(1)

if __name__=='__main__':main()
