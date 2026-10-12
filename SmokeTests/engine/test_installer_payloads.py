"""Execute the real ownership/removal scripts; does not install drivers or registry state."""
import hashlib,itertools,json,subprocess,argparse
from pathlib import Path

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    source=Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True,exist_ok=True);results=[]
    def run(root,manifest,ownership,mode):
        completed=subprocess.run(['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',str(source/'Setup/RemoveOwnedFiles.ps1'),'-Root',str(root),'-ManifestPath',str(manifest),'-OwnershipPath',str(ownership),'-Mode',mode],capture_output=True,text=True,timeout=30)
        if completed.returncode:raise RuntimeError(completed.stdout+completed.stderr)
    def digest(data):return hashlib.sha256(data).hexdigest()
    for index,flags in enumerate(itertools.product([False,True],repeat=4)):
        ir,calibration,rack,restore=flags;case=args.output/f'case-{index:02d}';case.mkdir()
        root=case/'IRs';root.mkdir();ownership=case/'.eqapo-owned-ir.json';manifest=case/'ir-manifest.json'
        bundled={'owned.wav':b'bundled-owned','modified.wav':b'bundled-modified','preexisting.wav':b'bundled-preexisting'}
        manifest.write_text(json.dumps([dict(path=name,sha256=digest(data)) for name,data in bundled.items()]))
        (root/'preexisting.wav').write_bytes(bundled['preexisting.wav'])
        (root/'user.wav').write_bytes(b'user-ir')
        (case/'config.txt').write_bytes(b'user configuration')
        run(root,manifest,ownership,'Prepare')
        for name,data in bundled.items():
            if not (root/name).exists():(root/name).write_bytes(data)
        (root/'modified.wav').write_bytes(b'user modification')
        if ir:run(root,manifest,ownership,'Prepare')
        else:run(root,manifest,ownership,'Remove')
        assert (root/'owned.wav').exists()==ir
        assert (root/'modified.wav').read_bytes()==b'user modification'
        assert (root/'preexisting.wav').read_bytes()==bundled['preexisting.wav']
        assert (root/'user.wav').read_bytes()==b'user-ir'
        assert (case/'config.txt').read_bytes()==b'user configuration'
        calroot=case/'HeadphoneCalibrations';calroot.mkdir();calowner=case/'.eqapo-owned-calibration.json';calmanifest=case/'cal-manifest.json'
        catalog=b'bundled catalog';calmanifest.write_text(json.dumps([dict(path='catalog.json',sha256=digest(catalog))]))
        run(calroot,calmanifest,calowner,'Prepare');(calroot/'catalog.json').write_bytes(catalog);(calroot/'user.json').write_bytes(b'user catalog')
        if not calibration:run(calroot,calmanifest,calowner,'Remove')
        assert (calroot/'catalog.json').exists()==calibration
        assert (calroot/'user.json').read_bytes()==b'user catalog'
        results.append(dict(status='PASS',bundled_ir=ir,headphone_calibration=calibration,rack_actions=rack,restore_point=restore,checks=['actual ownership script','modified IR preserved','identical preexisting IR preserved','user config preserved','owned calibration removal'],limits='Rack/restore flags enumerated only; actual NSIS deployment, registry, APO registration, System Restore and rollback are not executed.'))
        print(index,'PASS',flags,flush=True)
    (args.output/'results.json').write_text(json.dumps(results,indent=2))
if __name__=='__main__':main()
