"""Bounded configurable real-filter grid; automatic SDK precision is explicit."""
from case_inputs import load_cases
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--outproc-first', action='store_true')
    parser.add_argument('--cases', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    cases = load_cases(args.cases)
    rows = []
    for entry in cases:
        plugin = Path(entry['path'])
        for mode in (('OUTPROC', 'INPROC') if args.outproc_first else ('INPROC', 'OUTPROC')):
            for rate in (44100, 48000, 96000):
                for block in (32, 64, 128, 256, 480, 512, 960):
                    stem = f'{len(rows):03d}-{mode}-{rate}-{block}'
                    report = args.output / f'{stem}.json'
                    command = [str(args.probe.resolve()), '--plugin', str(plugin), '--mode', mode,
                               '--out', str(report.resolve()), '--rate', str(rate), '--block', str(block),
                               *entry['probe_arguments']]
                    timed_out = False
                    with (args.output / f'{stem}.log').open('wb') as log:
                        child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
                        try:
                            child.wait(timeout=12)
                        except subprocess.TimeoutExpired:
                            timed_out = True
                            subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'],
                                           stdout=log, stderr=subprocess.STDOUT, timeout=5)
                            if child.poll() is None:
                                child.kill()
                            child.wait(timeout=5)
                    result = json.loads(report.read_text()) if report.exists() else {}
                    ratio = (result['edited_rms'] / result['baseline_rms']
                             if result.get('baseline_rms', 0) > 0 else None)
                    expected = entry['expected_ratio']
                    passed = (child.returncode == 0 and result.get('status') == 'PASS'
                              and result.get('sample_rate') == rate and result.get('block_size') == block
                              and ratio is not None and abs(ratio / expected - 1) < .035)
                    row = {'plugin': str(plugin), 'mode': mode, 'sample_rate': rate, 'block_size': block,
                           'status': 'BOUNDED_RATE_BLOCK_PCM_PASS' if passed else
                           'TIMEOUT_RECOVERED' if timed_out else 'NEEDS_DIAGNOSIS',
                           'process_exit': child.returncode, 'ratio': ratio, 'expected_ratio': expected,
                           'probe': result, 'evidence': report.name,
                           'precision': 'HOST_DOUBLE_PLUGIN_SDK_AUTOMATIC'}
                    rows.append(row)
                    summary = {'cases': rows, 'passed': sum(r['status'] == 'BOUNDED_RATE_BLOCK_PCM_PASS' for r in rows),
                               'limits': 'GUI closed; two load-unload measurements, automatic plugin SDK precision; no full lifecycle or installed audiodg certification'}
                    temporary = args.output / 'summary.tmp'
                    temporary.write_text(json.dumps(summary, indent=2))
                    temporary.replace(args.output / 'summary.json')
                    print(f'{len(rows)}/{len(cases)*42} {plugin.name} {mode} {rate}/{block}: {row["status"]}', flush=True)
    return 0 if all(row['status'] == 'BOUNDED_RATE_BLOCK_PCM_PASS' for row in rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
