"""Inventory-driven bounded actual Qt lifecycle audits."""
import argparse
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--inventory', type=Path, required=True)
    plane = parser.add_mutually_exclusive_group(required=True)
    plane.add_argument('--editor', type=Path)
    plane.add_argument('--native-host', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--family', required=True)
    args = parser.parse_args()
    inventory = json.loads(args.inventory.read_text())
    entries = [row for row in inventory if args.family.lower() in row['path'].lower()]
    args.output.mkdir(parents=True, exist_ok=True)
    script = Path(__file__).with_name('test_qt_plugin_dialog.py' if args.editor else 'test_native_panel.py')
    results = []
    for index, entry in enumerate(entries):
        directory = args.output / str(index)
        with (args.output / f'{index}.log').open('wb') as log:
            subprocess.run([sys.executable, str(script), '--editor' if args.editor else '--host', str((args.editor or args.native_host).resolve()),
                            '--plugin', entry['path'], '--output', str(directory.resolve())],
                           stdout=log, stderr=subprocess.STDOUT, timeout=35)
        report = directory / ('bounded-result.json' if args.editor else 'result.json')
        result = json.loads(report.read_text()) if report.exists() else {
            'plugin': entry['path'], 'status': 'GUI_FAILURE', 'reason': 'MISSING_REPORT'}
        result['plugin'] = entry['path']
        result['gui_plane'] = 'INPROC_QT' if args.editor else 'OUTPROC_NATIVE'
        results.append(result)
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2))
        print(index, result['status'], entry['path'], flush=True)
    return 0 if results and all(row['status'] == 'PASS' for row in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
