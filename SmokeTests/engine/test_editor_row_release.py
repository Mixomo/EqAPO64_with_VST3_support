"""Bounded real Qt row lifetime batch, five cycles per owned Editor process."""
from case_inputs import load_cases
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--editor', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cases', type=Path, required=True)
    args = parser.parse_args()
    plugins = [Path(entry['path']) for entry in load_cases(args.cases)]
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    for index, plugin in enumerate(plugins):
        folder = args.output / f'{index:03d}'
        folder.mkdir()
        with (folder / 'process.log').open('wb') as log:
            child = subprocess.Popen([str(args.editor.resolve()), '--row-release-audit',
                                      str(plugin), str(folder.resolve())], stdout=log, stderr=subprocess.STDOUT)
            try:
                child.wait(timeout=25)
            except subprocess.TimeoutExpired:
                child.kill()  # Pinned owned Windows process handle.
                child.wait(timeout=5)
        report = folder / 'result.json'
        result = json.loads(report.read_text()) if report.exists() else {
            'status': 'TIMEOUT_RECOVERED', 'plugin': str(plugin), 'cycles': []}
        if child.returncode != 0 and result['status'] == 'PASS':
            result['status'] = 'FAIL_HOST'
        result['process_exit'] = child.returncode
        result['evidence'] = str(report.resolve())
        rows.append(result)
        temporary = args.output / 'summary.tmp'
        temporary.write_text(json.dumps({'plugins': len(rows),
            'passed': sum(row['status'] == 'PASS' for row in rows),
            'verified_cycles': sum(len(row['cycles']) for row in rows if row['status'] == 'PASS'),
            'cases': rows}, indent=2))
        temporary.replace(args.output / 'summary.json')
        print(f'{index + 1}/{len(plugins)} {plugin.name}: {result["status"]}', flush=True)
    return 0 if all(row['status'] == 'PASS' for row in rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
