"""Open real isolated Editor documents and inspect stored lines/visual placeholders."""
import argparse,json,subprocess
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--editor',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    cases=[('empty',b'',[]),('single-lf',b'Preamp: -3 dB\n',['Preamp: -3 dB']),('single-no-lf',b'Preamp: -3 dB',['Preamp: -3 dB']),('many',b'Preamp: -3 dB\n\nPreamp: -2 dB\n',['Preamp: -3 dB','','Preamp: -2 dB']),('intentional-blank',b'Preamp: -3 dB\n\n',['Preamp: -3 dB','']),('utf8-bom',b'\xef\xbb\xbfPreamp: -3 dB\n',['Preamp: -3 dB']),('bom-only',b'\xef\xbb\xbf',[])]
    results=[]
    for name,data,expected in cases:
        config=(args.output/(name+'.txt')).resolve();config.write_bytes(data)
        for repeat in range(2):
            directory=(args.output/(name+'-'+str(repeat))).resolve();directory.mkdir(exist_ok=True)
            with (directory/'console.log').open('wb') as log:run=subprocess.run([str(args.editor.resolve()),'--ui-audit',str(directory),str(config)],stdout=log,stderr=subprocess.STDOUT,timeout=20)
            geometry=json.loads((directory/'geometry.json').read_text());document=next(d for d in geometry['documents'] if d['path'].casefold()==str(config).casefold())
            passed=run.returncode==0 and document['lines']==expected and document['empty_placeholders']==(0 if expected else 1)
            results.append({'case':name,'fresh_open':repeat,'status':'PASS' if passed else 'FAIL','expected_lines':expected,'document':document})
    (args.output/'results.json').write_text(json.dumps(results,indent=2));print('PASS' if all(r['status']=='PASS' for r in results) else 'FAIL',len(results),'fresh Editor opens')
    if any(r['status']!='PASS' for r in results):raise SystemExit(1)

if __name__=='__main__':main()
