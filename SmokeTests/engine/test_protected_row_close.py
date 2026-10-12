"""Bounded actual Qt protected-row shutdown with optional owned fixture faults."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--editor', type=Path, required=True)
    parser.add_argument('--plugin', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', action='store_true')
    parser.add_argument('--hang-close', action='store_true')
    args = parser.parse_args()
    if args.hang_close and not args.fixture:
        parser.error('Fault injection is restricted to the owned fixture')
    args.output.mkdir(parents=True, exist_ok=True)
    if (args.output / 'result.json').exists():
        raise RuntimeError('Fresh output directory required')
    environment = os.environ.copy()
    environment.pop('EQAPO_FIXTURE_GUI_CLOSE_HANG', None)
    marker = (args.output / 'fixture-lifecycle.log').resolve()
    environment['EQAPO_FIXTURE_GUI_CLOSE_MARKER'] = str(marker)
    if args.hang_close:
        environment['EQAPO_FIXTURE_GUI_CLOSE_HANG'] = '1'
    child = None
    start = time.monotonic()
    try:
        with (args.output / 'process.log').open('wb') as log:
            child = subprocess.Popen([str(args.editor.resolve()), '--protected-close-audit',
                                      str(args.plugin.resolve()), str(args.output.resolve())],
                                     env=environment, stdout=log, stderr=subprocess.STDOUT)
            child.wait(timeout=15)
        result = json.loads((args.output / 'result.json').read_text())
        stages = marker.read_text().splitlines() if marker.exists() else []
        lifecycle_passed = (not args.fixture or
                            ('VIEW_CLOSE_HANG' in stages if args.hang_close else
                             'VIEW_DETACHED' in stages and 'INSTANCE_DESTROYED' in stages))
        passed = child.returncode == 0 and result['status'] == 'PASS' and lifecycle_passed
        result.update(status='PASS' if passed else 'GUI_FAILURE', fixture_stages=stages,
                      injected_fixture_hang=args.hang_close, elapsed_seconds=time.monotonic()-start,
                      process_exit=child.returncode)
        (args.output / 'bounded-result.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2))
        return 0 if passed else 1
    finally:
        if child and child.poll() is None:
            subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
            if child.poll() is None:
                child.kill()
            child.wait(timeout=5)


if __name__ == '__main__':
    raise SystemExit(main())
