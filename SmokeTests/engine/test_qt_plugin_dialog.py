"""Bounded real Qt plugin-dialog lifecycle audit; no inferred compatibility PASS."""
import argparse
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--editor', type=Path, required=True)
    parser.add_argument('--plugin', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    try:
        with (args.output / 'process.log').open('wb') as log:
            result = subprocess.run([str(args.editor.resolve()), '--plugin-dialog-audit',
                                     args.plugin, str(args.output.resolve())], timeout=25,
                                    stdout=log, stderr=subprocess.STDOUT)
        passed = result.returncode == 0
        classification = 'PASS' if passed else 'GUI_FAILURE'
        reason = 'ACTUAL_QT_DIALOG_LIFECYCLE' if passed else 'PROCESS_FAILURE'
    except subprocess.TimeoutExpired:
        passed = False
        classification = 'GUI_FAILURE'
        reason = 'QT_PLUGIN_LIFECYCLE_TIMEOUT_OWN_PROCESS_KILLED'
    stages = args.output / 'lifecycle-stages.log'
    evidence = {'status': classification, 'reason': reason, 'plugin': args.plugin,
                'elapsed_seconds': time.monotonic() - start,
                'stages': stages.read_text().splitlines() if stages.exists() else [],
                'limits': 'One actual in-process Qt GUI close and instance teardown; no live APO DSP or parameter certification.'}
    (args.output / 'bounded-result.json').write_text(json.dumps(evidence, indent=2))
    print(json.dumps(evidence, indent=2))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
