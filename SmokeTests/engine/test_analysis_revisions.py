"""Bounded request-replacement trials in one actual AnalysisThread instance."""
from case_inputs import load_cases
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cases', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    cases = load_cases(args.cases)
    results = []
    for entry in cases:
        name, plugin = entry['name'], Path(entry['path'])
        for mode in ('VSTPlugin', 'OutProcVSTPlugin'):
            for initial, final in entry['transitions']:
                label = f'{name}-{mode}-{initial}-{final}'
                configs = []
                for index, value in enumerate((initial, final)):
                    path = args.output / f'{label}-{index}.txt'
                    path.write_text(f'{mode}: Library "{plugin}" {entry["used_key"]} 1 {entry["gain_key"]} {value}\n')
                    configs.append(path)
                report = args.output / f'{label}.json'
                with (args.output / f'{label}.log').open('wb') as log:
                    child = subprocess.run([str(args.probe.resolve()), str(configs[0].resolve()),
                                            str(report.resolve()), '65536', str(configs[1].resolve())],
                                           stdout=log, stderr=subprocess.STDOUT, timeout=25)
                data = json.loads(report.read_text())
                gain = next(row['gain_db'] for row in data['response'] if row['hz'] == 1000)
                expected = (final - entry['neutral_value']) * entry['gain_db_per_unit']
                passed = (child.returncode == 0 and data['analysis_status'] == 0
                          and data['request_revision'] == data['result_revision'] == '2'
                          and gain is not None and abs(gain - expected) < .35)
                results.append({'case': label, 'plugin': str(plugin), 'mode': mode,
                                'status': 'PASS' if passed else 'FAIL', 'gain_db': gain,
                                'expected_db': expected, 'evidence': report.name})
                print(f'{label}: {results[-1]["status"]}', flush=True)
    (args.output / 'summary.json').write_text(json.dumps({
        'cases': results, 'limits': 'Timed request replacement in actual worker; not a deterministic reproduction of the narrow post-FFT race or MainWindow GUI workflow'}, indent=2))
    return 0 if all(row['status'] == 'PASS' for row in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
