"""Bounded real-filter probes; incomplete coverage never becomes full compatibility PASS."""
import argparse,json,subprocess,time,os
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--inventory',type=Path,required=True);p.add_argument('--probe',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--scope',choices=['focused','all'],default='focused');p.add_argument('--timeout',type=int,default=20);p.add_argument('--resume',action='store_true');args=p.parse_args()
    if not 1<=args.timeout<=60:p.error('timeout must be 1..60 seconds')
    args.output.mkdir(parents=True,exist_ok=True);rows=[]
    if args.resume and (args.output/'matrix.json').exists():rows=json.loads((args.output/'matrix.json').read_text())
    completed={(row['path'].casefold(),row['mode']) for row in rows}
    plugins=json.loads(args.inventory.read_text())
    for index,entry in enumerate(plugins):
        path=entry['path'];lower=path.lower()
        if args.scope=='focused' and 'goodhertz' not in lower and 'pro-q' not in lower:continue
        for mode in ('INPROC','OUTPROC'):
            if (path.casefold(),mode) in completed:continue
            stem=f'{index:04d}-{mode}';report=(args.output/(stem+'.json')).resolve()
            row={'path':path,'mode':mode,'status':'INCOMPLETE','required_checks_remaining':['live_GUI_parameter_PCM','GUI_animation','bypass_100','reload_100','load_unload_100','GUI_close_reopen','native_resize','live_DPI','analyzer','recovery','performance','installed_audiodg']}
            if entry['architecture']!='x64':row.update(status='BLOCKED_ENVIRONMENT',reason='NOT_X64')
            else:
                command=[str(args.probe.resolve()),'--plugin',path,'--mode',mode,'--out',str(report)]
                # These IDs were queried and verified only for Q3 VST2/Q4 VST3.
                # Other versions need their own metadata/oracle.
                if Path(path).name.casefold() in ('fabfilter pro-q 3.dll','fabfilter pro-q 4.vst3'):
                    used,gain=('#0','#3') # Actual Q3 VST2 indices and Q4 VST3 ParamIDs.
                    command+=['--baseline-set',used,'1','--baseline-set',gain,'.5','--edit-set',gain,'.65','--require-delta']
                start=time.monotonic()
                with (args.output/(stem+'.log')).open('wb') as log:
                    try:
                        run=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
                        try:run.wait(timeout=args.timeout)
                        except subprocess.TimeoutExpired:
                            # Kill only this owned probe and its descendant host;
                            # killing the parent alone leaves inherited IPC alive.
                            if os.name=='nt':
                                subprocess.run(['taskkill','/PID',str(run.pid),'/T','/F'],stdout=log,stderr=subprocess.STDOUT,timeout=5)
                            else:run.kill()
                            if run.poll() is None:run.kill()
                            run.wait(timeout=5)
                            raise
                        row['exit_code']=run.returncode
                        if report.exists():row['probe']=json.loads(report.read_text())
                        if row.get('probe',{}).get('status')!='PASS':row.update(status='NEEDS_DIAGNOSIS',reason=row.get('probe',{}).get('reason',row.get('probe',{}).get('status','CRASH_OR_NO_REPORT')))
                    except subprocess.TimeoutExpired:row.update(status='TIMEOUT_RECOVERED',reason='ISOLATED_PROBE_KILLED_NO_LICENSE_INFERENCE')
                    except (OSError,ValueError) as error:row.update(status='NEEDS_DIAGNOSIS',reason=str(error))
                row['seconds']=round(time.monotonic()-start,3)
            rows.append(row)
            temporary=args.output/'matrix.json.tmp';temporary.write_text(json.dumps(rows,indent=2));temporary.replace(args.output/'matrix.json')
            print(index,Path(path).name,mode,row['status'],row.get('reason',''),flush=True)
    counts={}
    for row in rows:counts[row['status']]=counts.get(row['status'],0)+1
    (args.output/'summary.json').write_text(json.dumps({'counts':counts,'full_compatibility_passes':0,'scope':args.scope},indent=2))

if __name__=='__main__':main()
