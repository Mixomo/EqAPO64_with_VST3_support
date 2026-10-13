"""Verify independent and fixed VST layouts against virtual stereo/5.1/7.1 devices."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--harness', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    for mode in ('input-only', 'output-only', 'fixed-stereo', 'fixed-surround', 'symmetric'):
        report = (args.output / (mode + '.json')).resolve()
        with (args.output / (mode + '.log')).open('wb') as log:
            run = subprocess.run([str(args.harness.resolve()), str(args.fixture.resolve()),
                                  str(args.host.resolve()), str(report), mode],
                                 stdout=log, stderr=subprocess.STDOUT, timeout=20)
        result = json.loads(report.read_text(encoding='utf-8'))
        assert run.returncode == 0 and result['status'] == 'PASS', (mode, result)
        result['mode'] = mode
        results.append(result)
        print(mode + ': PASS', flush=True)
    (args.output / 'summary.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
