"""Bounded real-DSP native views; this does not certify Editor/service binding."""
from case_inputs import load_cases
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--fixture-vst2', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mmcss', action='store_true')
    parser.add_argument('--cases', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    cases = []
    for rate, block in [(44100, 32), (48000, 480), (96000, 32)]:
        cases.append((f'fixture-{rate}-{block}', args.fixture, 7, 1, .25, .25, rate, block, [], True))
    cases.append(('fixture-vst2', args.fixture_vst2, 0, 1, .25, .25, 48000, 480, [], False))
    for entry in load_cases(args.cases):
        cases.append((entry['name'], Path(entry['path']), entry['parameter_id'],
                      entry['before'], entry['after'], entry['expected_ratio'],
                      entry.get('rate', 48000), entry.get('block', 480), entry.get('extra', []), False))
    summary = []
    for name, plugin, ident, before, after, ratio, rate, block, extra, meter in cases:
        folder = args.output / name
        folder.mkdir()
        environment = os.environ.copy()
        environment['EQAPO_NATIVE_DSP_VIEW_AUDIT'] = str((folder/'view.jsonl').resolve())
        command = [str(args.probe.resolve()), str(plugin.resolve()), str((folder/'pcm.json').resolve()),
                   str(ident), str(before), str(after), str(ratio), '--verify-native-view-detach',
                   '--rate', str(rate), '--block', str(block), *extra]
        if args.mmcss: command.append('--mmcss')
        started = time.monotonic()
        try:
            with (folder/'process.log').open('wb') as log:
                process = subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=20)
            pcm = json.loads((folder/'pcm.json').read_text())
            events = [json.loads(line) for line in (folder/'view.jsonl').read_text().splitlines()]
            attached = any(row['event']=='REAL_COMPONENT_VIEW_ATTACHED' for row in events)
            closed = any(row['event']=='VIEW_DETACHED_DSP_RETAINED' and row['elapsed_ms']<6000 for row in events)
            baseline, edited = [], []
            for row in events:
                match = re.fullmatch(r'Fixture actual output RMS ([0-9.]+)', row.get('text', ''))
                if match:
                    if 1500<row['elapsed_ms']<2700: baseline.append(float(match[1]))
                    if 3700<row['elapsed_ms']<4800: edited.append(float(match[1]))
            meter_pass = (not meter or (len(baseline)>=5 and len(edited)>=5
                          and abs(statistics.median(baseline)-.0707107)<.003
                          and abs(statistics.median(edited)-.0176777)<.001))
            passed = process.returncode==0 and pcm['status']=='PASS' and attached and closed and meter_pass
            record = dict(case=name, plugin=str(plugin.resolve()), status='PASS' if passed else 'FAIL_HOST', process_exit=process.returncode,
                          real_component_view_attached=attached, view_detached_before_final_pcm=closed,
                          native_meter_verified=meter and meter_pass,
                          baseline_meter_median=statistics.median(baseline) if baseline else None,
                          edited_meter_median=statistics.median(edited) if edited else None,
                          pcm=pcm, limits='Interactive private DSP host only; production Editor/service-session binding, vendor meters and installed audiodg remain unverified')
        except Exception as error:
            record = dict(case=name, plugin=str(plugin.resolve()), status='FAIL_HOST', error=str(error))
        record['elapsed_seconds'] = time.monotonic()-started
        (folder/'result.json').write_text(json.dumps(record, indent=2))
        summary.append(record)
        (args.output/'summary.json').write_text(json.dumps(summary, indent=2))
        print(json.dumps(record), flush=True)
    return 0 if all(row['status']=='PASS' for row in summary) else 1


if __name__ == '__main__':
    raise SystemExit(main())
