"""Check plugin scanning and actual Editor row roundtrips on supplied writable drives."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--editor', type=Path, required=True)
    parser.add_argument('--plugin', type=Path, required=True)
    parser.add_argument('--roots', type=Path, nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    host, editor, source = args.host.resolve(), args.editor.resolve(), args.plugin.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    for index, root in enumerate(args.roots):
        if not root.is_dir():
            parser.error('Each root must be an existing writable directory')
        with tempfile.TemporaryDirectory(prefix='EqApo drive \u00f1 ', dir=root) as directory:
            destination = Path(directory) / source.name
            if source.is_dir():
                shutil.copytree(source, destination)
            else:
                shutil.copy2(source, destination)
            case = (args.output / str(index)).resolve()
            case.mkdir(exist_ok=True)
            report = case / 'scan.json'
            run = subprocess.run([str(host), '--scan', '--plugin', str(destination),
                                  '--result', str(report)], capture_output=True, timeout=15)
            scan = json.loads(report.read_text(encoding='utf-8'))
            assert run.returncode == 0 and scan['status'] == 'PASS', scan
            run = subprocess.run([str(editor), '--control-identity-audit', str(destination),
                                  str(case)], capture_output=True, timeout=20)
            (case / 'editor-console.log').write_bytes(run.stdout + run.stderr)
            assert run.returncode == 0, ('Editor audit failed', run.returncode, str(case))
            row = json.loads((case / 'result.json').read_text(encoding='utf-8'))
            assert run.returncode == 0 and row['status'] == 'PASS', row
            for item in row['cases']:
                assert str(destination) in item['parameters'], item
                assert item['restored'] == item['parameters'], item
            results.append({'drive': destination.drive, 'status': 'PASS',
                            'scanner': scan, 'editor_roundtrips': len(row['cases'])})
    (args.output / 'summary.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print('PASS', len(results), 'drive-path cases')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
