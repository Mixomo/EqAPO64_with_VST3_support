"""100-cycle regression split into independently bounded, owned-process shards."""
import argparse
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--plugin', type=Path, required=True)
    parser.add_argument('--mode', choices=['INPROC', 'OUTPROC'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cycles', type=int, default=100)
    parser.add_argument('--shard', type=int, default=5)
    parser.add_argument('--gain', type=float, required=True)
    parser.add_argument('--set', nargs=2, action='append', default=[])
    parser.add_argument('--resume', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.shard <= 10 or not 1 <= args.cycles <= 100:
        parser.error('cycles 1..100, shard 1..10')
    args.output.mkdir(parents=True, exist_ok=True)
    summary_path = args.output / 'summary.json'
    rows = []
    if args.resume and summary_path.exists():
        previous = json.loads(summary_path.read_text())
        if (previous['plugin'], previous['mode'], previous['gain'], previous['parameters']) != (str(args.plugin.resolve()), args.mode, args.gain, args.set):
            raise RuntimeError('Resume identity or oracle mismatch')
        rows = previous['shards']
    elif summary_path.exists():
        raise RuntimeError('Existing evidence: use --resume or a fresh directory')
    completed = sum(row.get('result', {}).get('cycles', 0) for row in rows if row['status'] == 'PASS')
    while completed < args.cycles:
        count = min(args.shard, args.cycles - completed)
        stem = f'shard-{len(rows):03d}'
        report = (args.output / (stem + '.json')).resolve()
        command = [str(args.probe.resolve()), str(args.plugin.resolve()), args.mode,
                   str(report), str(count), str(args.gain)]
        for key, value in args.set:
            command.extend([key, value])
        start = time.monotonic()
        row = {'status': 'FAIL_HOST', 'report': report.name}
        with (args.output / (stem + '.log')).open('wb') as log:
            child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            try:
                child.wait(timeout=50)
                row['exit_code'] = child.returncode
                if report.exists():
                    row['result'] = json.loads(report.read_text())
                if child.returncode == 0 and row.get('result', {}).get('status') == 'PASS':
                    row['status'] = 'PASS'
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'],
                               stdout=log, stderr=subprocess.STDOUT, timeout=5)
                if child.poll() is None:
                    child.kill()
                child.wait(timeout=5)
                row['status'] = 'TIMEOUT_RECOVERED'
        row['seconds'] = round(time.monotonic() - start, 3)
        rows.append(row)
        completed += count if row['status'] == 'PASS' else 0
        summary = {'status': 'PASS' if completed == args.cycles else 'INCOMPLETE',
                   'plugin': str(args.plugin.resolve()), 'mode': args.mode, 'gain': args.gain,
                   'parameters': args.set, 'requested_cycles': args.cycles, 'verified_cycles': completed,
                   'bypass_unbypass': completed, 'reload': completed, 'load_unload': completed,
                   'shard_timeout_seconds': 50, 'shards': rows,
                   'limits': 'Actual real-filter graphs with continuous independent PCM clock; GUI closed, 48k/480 f64; no Editor UI or installed audiodg certification.'}
        temporary = summary_path.with_suffix('.json.tmp')
        temporary.write_text(json.dumps(summary, indent=2))
        temporary.replace(summary_path)
        print(args.mode, args.plugin.name, completed, '/', args.cycles, row['status'], flush=True)
        if row['status'] != 'PASS':
            return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
