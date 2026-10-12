"""Bounded actual Editor analyzer checks with PCM-derived response oracles."""
from case_inputs import load_cases
import argparse
import json
import math
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cases', type=Path)
    args = parser.parse_args()
    if args.fixture.suffix.casefold() != '.vst3' or not args.fixture.exists():
        parser.error('This oracle requires the owned VST3 fixture (parameter ID 7, latency 257)')
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []

    def check(name, command, expected_db, latency=None, bom=False, fault=None, shape=None):
        config = args.output / (name + '.txt')
        report = args.output / (name + '.json')
        config.write_text(command + '\n', encoding='utf-8-sig' if bom else 'utf-8')
        environment = os.environ.copy()
        environment.pop('EQAPO_FIXTURE_FAULT', None)
        if fault:
            environment['EQAPO_FIXTURE_FAULT'] = fault
        with (args.output / (name + '.log')).open('wb') as log:
            run = subprocess.run([str(args.probe.resolve()), str(config.resolve()),
                                  str(report.resolve()), '65536'], env=environment,
                                 stdout=log, stderr=subprocess.STDOUT, timeout=25)
        data = json.loads(report.read_text())
        if fault:
            passed = (run.returncode == 1 and data['analysis_status'] != 0
                      and data['peak_db'] is None and not data['response']
                      and data['elapsed_ms'] < 10000)
        else:
            response = next(x['gain_db'] for x in data['response'] if x['hz'] == 1000)
            passed = (run.returncode == 0 and data['analysis_status'] == 0
                      and math.isfinite(response) and abs(response - expected_db) < .35
                      and (latency is None or data['latency_samples'] == latency)
                      and math.isfinite(data['cpu_processing_ms'])
                      and data['cpu_processing_ms'] >= 0)
            samples = {x['hz']: x['gain_db'] for x in data['response']}
            if shape == 'flat':
                passed = passed and all(value is not None and math.isfinite(value)
                                        and abs(value) < .35 for value in samples.values())
            elif shape == 'bell':
                # A constant gain can pass the center-frequency oracle but is
                # not a bell. Require near-unity distant bands, shoulders of
                # the correct sign, and a larger center magnitude.
                sign = 1 if expected_db > 0 else -1
                passed = passed and all(samples.get(hz) is not None and
                                        math.isfinite(samples[hz]) for hz in (100, 500, 2000, 10000))
                if passed:
                    passed = (abs(samples[100]) < .5 and abs(samples[10000]) < .5
                              and all(1 < sign * samples[hz] < abs(response) - .5
                                      for hz in (500, 2000)))
        rows.append({'case': name, 'status': 'PASS' if passed else 'FAIL',
                     'expected_1000hz_db': expected_db, 'exit_code': run.returncode,
                     'elapsed_ms': data['elapsed_ms'], 'evidence': report.name,
                     'response_shape_oracle': shape})

    for mode in ('VSTPlugin', 'OutProcVSTPlugin'):
        command = f'{mode}: Library "{args.fixture.resolve()}" #7 0.25'
        for bom in (False, True):
            check(f'fixture-{mode}-bom-{int(bom)}', command, -12.0411998266, 257, bom)
        check(f'fixture-{mode}-hang', command, None, fault='hang')
    for entry in load_cases(args.cases):
        plugin = Path(entry['path'])
        for mode in ('VSTPlugin', 'OutProcVSTPlugin'):
            for oracle in entry['analysis_oracles']:
                command = f'{mode}: Library "{plugin}" {oracle["parameters"]}'
                check(f'{entry["name"]}-{mode}-{oracle["name"]}', command,
                      oracle['expected_db'], shape=oracle.get('shape'))
    (args.output / 'summary.json').write_text(json.dumps({
        'cases': rows, 'limits': 'Isolated actual AnalysisThread; no live GUI edits, shelves, HP/LP, physical DPI or installed audiodg certification.'}, indent=2))
    print(json.dumps(rows, indent=2))
    return 0 if all(row['status'] == 'PASS' for row in rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
