"""Sequential GUI regression batch; every child trial has its own bound."""
import argparse
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--editor', type=Path, required=True)
    parser.add_argument('--plugin-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    plugins = sorted(args.plugin_root.glob('*.vst3'))
    if not plugins:
        parser.error('No VST3 plugins found in the explicit root')
    args.output.mkdir(parents=True, exist_ok=False)
    entries = []
    for index, plugin in enumerate(plugins):
        folder = args.output / f'{index:03d}'
        with (args.output / f'{index:03d}.log').open('wb') as log:
            run = subprocess.run([sys.executable, str(Path(__file__).with_name('test_protected_row_close.py')),
                                  '--editor', str(args.editor.resolve()), '--plugin', str(plugin.resolve()),
                                  '--output', str(folder.resolve())], stdout=log, stderr=subprocess.STDOUT)
        result_path = folder / 'bounded-result.json'
        result = json.loads(result_path.read_text()) if result_path.exists() else {'status': 'GUI_FAILURE'}
        entries.append({'plugin': str(plugin.resolve()), 'driver_exit': run.returncode,
                        'result': str(result_path.resolve()), **result})
        summary = {'scope': 'Actual protected Qt row GUI close only; no full compatibility certification',
                   'count': len(entries), 'pass': sum(e['status'] == 'PASS' for e in entries),
                   'entries': entries}
        temporary = args.output / 'summary.tmp'
        temporary.write_text(json.dumps(summary, indent=2), encoding='utf-8')
        temporary.replace(args.output / 'summary.json')
        print(f'{index + 1}/{len(plugins)} {plugin.name}: {result["status"]}', flush=True)
    return 0 if all(e['status'] == 'PASS' for e in entries) else 1


if __name__ == '__main__':
    raise SystemExit(main())
