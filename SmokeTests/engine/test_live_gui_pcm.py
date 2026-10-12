"""Real native GUI edit while one private DSP filter remains alive."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def stop_owned(process):
    if process and process.poll() is None:
        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
        if process.poll() is None:
            process.kill()
        process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--plugin', type=Path, required=True)
    parser.add_argument('--id', type=int, required=True)
    parser.add_argument('--before', type=float, required=True)
    parser.add_argument('--after', type=float, required=True)
    parser.add_argument('--ratio', type=float, required=True)
    parser.add_argument('--click', type=int, nargs=2, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--in-process', action='store_true')
    parser.add_argument('--verify-after-close', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = (args.output / 'pcm.json').resolve()
    if report.exists() or Path(str(report) + '.ready').exists():
        raise RuntimeError('Use a fresh output directory; refusing stale synchronization markers')
    session = 'live-gui-' + str(os.getpid())
    pcm = gui = None
    try:
        with (args.output / 'pcm.log').open('wb') as pcm_log, (args.output / 'gui.log').open('wb') as gui_log:
            pcm = subprocess.Popen([str(args.probe.resolve()), str(args.plugin.resolve()), str(report),
                                    str(args.id), str(args.before), str(args.after), str(args.ratio),
                                    '--gui-session', session] + (['--in-process'] if args.in_process else []) + (['--verify-after-close'] if args.verify_after_close else []), stdout=pcm_log, stderr=subprocess.STDOUT)
            gui = subprocess.Popen([sys.executable, str(Path(__file__).with_name('test_native_panel.py')),
                                    '--host', str(args.host.resolve()), '--plugin', str(args.plugin.resolve()),
                                    '--output', str((args.output / 'gui').resolve()), '--session', session,
                                    '--wait-before-click', str(report) + '.ready', '--hold-for-report', str(report) + ('.edited' if args.verify_after_close else ''),
                                    '--click', *map(str, args.click)], stdout=gui_log, stderr=subprocess.STDOUT)
            gui.wait(timeout=45)
            if args.verify_after_close and gui.returncode == 0:
                Path(str(report) + '.gui-closed').write_text('NATIVE_GUI_HOST_EXITED\n')
            pcm.wait(timeout=10)
        audio = json.loads(report.read_text())
        panel = json.loads((args.output / 'gui/result.json').read_text())
        changed = panel.get('parameter_changes', {}).get('#' + str(args.id), {})
        passed = (pcm.returncode == 0 and gui.returncode == 0 and audio['status'] == 'PASS'
                  and panel.get('gui_visible_through_pcm_report') is True
                  and abs(changed.get('after', -999) - args.after) < .001)
        result = {'status': 'PASS' if passed else 'PARAMETER_FAILURE', 'plugin': str(args.plugin),
                  'mode': 'IN_PROCESS' if args.in_process else 'OUT_OF_PROCESS',
                  'parameter': args.id, 'gui_change': changed, 'audio': audio,
                  'gui_visible_through_pcm': panel.get('gui_visible_through_pcm_report'),
                  'verified_audio_after_gui_close': args.verify_after_close and audio.get('gui_closed_before_measurement') is True,
                  'limits': 'Actual native protected GUI/controller to scoped control IPC to persistent ' + ('in-process' if args.in_process else 'OOP') + ' DSP/PCM; no Editor model, installed audiodg, meter animation or physical DPI certification.'}
        (args.output / 'result.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2))
        return 0 if passed else 1
    finally:
        stop_owned(gui)
        stop_owned(pcm)


if __name__ == '__main__':
    raise SystemExit(main())
