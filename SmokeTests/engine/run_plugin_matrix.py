"""Isolated, bounded plugin smoke matrix. No licensing inference from a timeout."""
import argparse, json, subprocess, time
from pathlib import Path

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--matrix',type=Path,required=True)
    parser.add_argument('--smoke',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--timeout',type=int,default=60)
    args=parser.parse_args()
    if not 1<=args.timeout<=60:parser.error('--timeout must be between 1 and 60 seconds')
    args.output.mkdir(parents=True,exist_ok=True)
    results=[]
    for index,entry in enumerate(json.loads(args.matrix.read_text())['plugins']):
        path=Path(entry['path']); row={'path':str(path),'required_checks':entry['required_checks']}
        report=args.output/f'{index:03d}.json'
        if not path.exists():
            row.update(status='BLOCKED',reason='PLUGIN_NOT_INSTALLED')
        else:
            started=time.monotonic()
            with (args.output/f'{index:03d}.log').open('wb') as log:
                try:
                    process=subprocess.run([str(args.smoke),'--plugin',str(path),'--out',str(report),'--gui'],stdout=log,stderr=subprocess.STDOUT,timeout=args.timeout)
                    row['exit_code']=process.returncode
                    if report.exists(): row['smoke']=json.loads(report.read_text())
                    row['status']='PASS' if process.returncode==0 and row.get('smoke',{}).get('status')=='PASS' else 'FAIL'
                    if row['status']=='FAIL': row['reason']=row.get('smoke',{}).get('stage','PROCESS_CRASH_OR_NO_REPORT')
                except subprocess.TimeoutExpired:
                    row.update(status='FAIL',reason='PROCESS_TIMEOUT_REQUIRES_DIAGNOSIS')
                except (OSError,ValueError) as error:
                    row.update(status='FAIL',reason=str(error))
            row['seconds']=round(time.monotonic()-started,3)
        row['limits']='Status covers the reported smoke checks only. Required checks absent from smoke.checks remain BLOCKED/NOT_EXECUTED; a timeout is not evidence of a licensing restriction.'
        results.append(row)
        (args.output/'matrix-results.json').write_text(json.dumps(results,indent=2))
        print(index,path.name,row['status'],row.get('reason',''),flush=True)

if __name__=='__main__': main()
