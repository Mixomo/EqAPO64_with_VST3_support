"""Actual analyzer shape checks selected by vendor SDK metadata, not enum guesses."""
import argparse
import json
import math
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--plugin', type=Path, required=True)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    metadata = json.loads(args.metadata.read_text(encoding='utf-8-sig'))
    by_title = {row['title']: row for row in metadata}
    used = by_title['Band 1 Used']['id']
    gain = by_title['Band 1 Gain']['id']
    shape = by_title['Band 1 Shape']
    choices = {row['text']: row['normalized'] for row in shape['choices']}
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    for mode in ('VSTPlugin', 'OutProcVSTPlugin'):
        for label in ('Low Shelf', 'High Shelf', 'Low Cut', 'High Cut'):
            for sign in ((1, -1) if 'Shelf' in label else (1,)):
                name = f'{mode}-{label.replace(" ", "-")}-{sign}'
                config, report = args.output / f'{name}.txt', args.output / f'{name}.json'
                config.write_text(f'{mode}: Library "{args.plugin.resolve()}" #{used} 1 '
                                  f'#{gain} {0.5 + sign * 0.1} #{shape["id"]} {choices[label]}\n')
                with (args.output / f'{name}.log').open('wb') as log:
                    run = subprocess.run([str(args.probe.resolve()), str(config.resolve()),
                                          str(report.resolve()), '65536'], stdout=log,
                                         stderr=subprocess.STDOUT, timeout=25)
                data = json.loads(report.read_text())
                values = {row['hz']: row['gain_db'] for row in data['response']}
                passed = run.returncode == 0 and data['analysis_status'] == 0
                passed = passed and all(values.get(hz) is not None and math.isfinite(values[hz])
                                        for hz in (100, 500, 1000, 2000, 10000))
                if passed and 'Shelf' in label:
                    boosted, unity = (100, 10000) if label == 'Low Shelf' else (10000, 100)
                    passed = (abs(values[boosted] - sign * 6) < .5
                              and abs(values[unity]) < .5
                              and 1 < sign * values[1000] < 5)
                elif passed:
                    rejected, retained = (100, 10000) if label == 'Low Cut' else (10000, 100)
                    passed = (values[rejected] < -15 and abs(values[retained]) < .5
                              and abs(values[1000] + 3.0103) < .7)
                    ordered = [values[hz] for hz in (100, 500, 1000, 2000, 10000)]
                    if label == 'High Cut':
                        ordered.reverse()
                    passed = passed and all(a < b for a, b in zip(ordered, ordered[1:]))
                rows.append({'case': name, 'status': 'PASS' if passed else 'FAIL',
                             'mode': mode, 'shape_label': label, 'sdk_shape_id': shape['id'],
                             'sdk_normalized_choice': choices[label], 'evidence': report.name,
                             'response': values, 'elapsed_ms': data['elapsed_ms']})
                print(f'{name}: {rows[-1]["status"]}', flush=True)
    (args.output / 'summary.json').write_text(json.dumps({
        'cases': rows, 'metadata_evidence': str(args.metadata.resolve()),
        'limits': 'One static default-frequency band per case; no GUI/preset updates, installed audiodg, or nonlinear certification'}, indent=2))
    return 0 if all(row['status'] == 'PASS' for row in rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
