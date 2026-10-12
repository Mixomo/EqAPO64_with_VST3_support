"""Actual selector and convolution-analysis regression without system installation."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import wave


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--editor', type=Path, required=True)
    parser.add_argument('--analyzer', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    root = args.editor.resolve().parent / 'IRs'
    owned = root / '_smoke_owned_fixture'
    if owned.exists():
        raise RuntimeError('Owned fixture already exists; refusing to overwrite')
    nested = owned / 'nested'
    nested.mkdir(parents=True)
    files = [owned / 'gain-half.wav', nested / 'gain-quarter.WAV']
    try:
        for file, gain in zip(files, (.5, .25)):
            with wave.open(str(file), 'wb') as output:
                output.setnchannels(1)
                output.setsampwidth(2)
                output.setframerate(48000)
                output.writeframes(struct.pack('<h', int(gain * 32768)) + bytes(126))
        with (args.output / 'selector.log').open('wb') as log:
            run = subprocess.run([str(args.editor.resolve()), '--bundled-ir-audit',
                                  str(args.output.resolve())], stdout=log,
                                 stderr=subprocess.STDOUT, timeout=25)
        selector = json.loads((args.output / 'selector.json').read_text())
        rows = []
        for file, expected in zip(files, (-6.0205999133, -12.0411998266)):
            selected = next((row for row in selector['selections']
                             if Path(row['path']).resolve() == file.resolve()), None)
            if selected is None:
                rows.append({'file': str(file), 'status': 'FAIL', 'reason': 'NOT_ENUMERATED'})
                continue
            config = args.output / (file.stem + '.txt')
            report = args.output / (file.stem + '.json')
            config.write_text('Convolution: ' + selected['parameters'] + '\n', encoding='utf-8')
            with (args.output / (file.stem + '.log')).open('wb') as log:
                analysis = subprocess.run([str(args.analyzer.resolve()), str(config.resolve()),
                                           str(report.resolve()), '65536'], stdout=log,
                                          stderr=subprocess.STDOUT, timeout=25)
            data = json.loads(report.read_text())
            passed = (analysis.returncode == 0 and selected['restored']
                      and abs(data['peak_db'] - expected) < .02
                      and all(abs(point['gain_db'] - expected) < .02 for point in data['response']))
            rows.append({'file': str(file), 'status': 'PASS' if passed else 'FAIL',
                         'expected_db': expected, 'measured_db': data['peak_db'],
                         'restored': selected['restored'], 'analysis_evidence': report.name})
        passed = run.returncode == 0 and selector['selector_visible'] and all(row['status'] == 'PASS' for row in rows)
        (args.output / 'result.json').write_text(json.dumps({
            'status': 'PASS' if passed else 'FAIL', 'cases': rows,
            'limits': 'Owned fixture IRs in candidate executable directory; actual Qt selection/restoration and convolution analysis; no installed audiodg or optional-feature-off proof.'}, indent=2))
        print('PASS' if passed else 'FAIL')
        return 0 if passed else 1
    finally:
        # Delete only the exact fixture files created above, with native Path operations.
        for file in files:
            if file.exists():
                file.unlink()
        nested.rmdir()
        owned.rmdir()
        if not any(root.iterdir()):
            root.rmdir()


if __name__ == '__main__':
    raise SystemExit(main())
