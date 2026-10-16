"""Actual Editor session and audio-stream persistence with three owned fixtures."""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import uuid

parser = argparse.ArgumentParser()
for name in ('editor', 'host', 'probe', 'fixture-vst3', 'fixture-vst2', 'output'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
for name in ('editor', 'host', 'probe', 'fixture_vst3', 'fixture_vst2'):
    path = getattr(args, name).resolve()
    if not path.is_file():
        parser.error('Supply existing binaries, with the Editor runtime deployed alongside it')
    setattr(args, name, path)
case = args.output.resolve() / ('editor-stream-' + uuid.uuid4().hex[:8])
case.mkdir(parents=True)
prefix = case / 'session'
config = Path(str(prefix) + '.config.txt')
sid = re.search(r'S-1-5-21-[0-9-]+', subprocess.check_output(['whoami', '/user'], text=True)).group()
kernel = C.WinDLL('kernel32', use_last_error=True)
user = C.WinDLL('user32', use_last_error=True)
kernel.OpenFileMappingW.argtypes = [C.c_uint, C.c_int, C.c_wchar_p]
kernel.OpenFileMappingW.restype = C.c_void_p
kernel.MapViewOfFile.argtypes = [C.c_void_p, C.c_uint, C.c_uint, C.c_uint, C.c_size_t]
kernel.MapViewOfFile.restype = C.c_void_p
kernel.UnmapViewOfFile.argtypes = [C.c_void_p]
kernel.CloseHandle.argtypes = [C.c_void_p]
callback = C.WINFUNCTYPE(C.c_int, C.c_void_p, C.c_ssize_t)
user.EnumWindows.argtypes = [callback, C.c_ssize_t]
user.GetWindowThreadProcessId.argtypes = [C.c_void_p, C.POINTER(C.c_uint)]
user.GetWindowTextW.argtypes = [C.c_void_p, C.c_wchar_p, C.c_int]

def mapping(name, size):
    handle = kernel.OpenFileMappingW(4, False, name)
    if not handle:
        return None
    try:
        view = kernel.MapViewOfFile(handle, 4, 0, 0, size)
        if not view:
            return None
        try:
            return C.string_at(view, size)
        finally:
            kernel.UnmapViewOfFile(view)
    finally:
        kernel.CloseHandle(handle)

def own_panels():
    pids = set()
    for session in re.findall(r'HostId\s+(\S+)', config.read_text(encoding='utf-8')):
        for space in ('Global', 'Local'):
            name = space + '\\EqApoControl_' + session + '_' + sid
            index = mapping(name + '_Current', 32)
            if not index:
                continue
            generation = struct.unpack_from('<Q', index, 16)[0]
            data = mapping(name + '_' + str(generation) + '_View', 40)
            if data:
                pids.add(struct.unpack_from('<I', data, 32)[0])
    found = []
    @callback
    def collect(window, _):
        pid = C.c_uint()
        user.GetWindowThreadProcessId(window, C.byref(pid))
        if pid.value in pids:
            title = C.create_unicode_buffer(128)
            user.GetWindowTextW(window, title, len(title))
            if title.value == 'Equalizer APO - VST':
                found.append(int(window))
        return True
    user.EnumWindows(collect, 0)
    return found

editors = []
events = []
env = os.environ.copy()
env.pop('EQAPO_STATE_TEST_GUI_READY', None)
env['EQAPO_STATE_TEST_EDITOR_SIGNAL'] = str(prefix)
command = [str(args.probe), str(args.host), str(args.fixture_vst3), str(args.fixture_vst2), str(args.fixture_vst3), '7', '.75']
with (case / 'broker.log').open('w', encoding='utf-8') as log:
    broker = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
    def wait(path):
        deadline = time.monotonic() + 12
        while not path.exists() and broker.poll() is None and time.monotonic() < deadline:
            time.sleep(.02)
        if not path.exists():
            raise RuntimeError('Missing signal: ' + str(path))
    def mark(suffix):
        Path(str(prefix) + suffix).write_text('ready')
    def open_editor(number):
        folder = case / ('editor-' + str(number))
        folder.mkdir()
        before = config.read_bytes()
        process = subprocess.Popen([str(args.editor), '--state-session-audit', str(config), str(folder)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        editors.append((process, folder))
        wait(folder / 'ready.json')
        time.sleep(.3)
        if own_panels() or config.read_bytes() != before:
            raise RuntimeError('Opening Editor changed the state or opened a panel automatically')
        invoke = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(Path(__file__).with_name('invoke_editor_panels.ps1')), '-EditorPID', str(process.pid), '-EditorPath', str(args.editor)], capture_output=True, text=True, timeout=10)
        if invoke.returncode:
            raise RuntimeError(invoke.stderr)
        events.append({'action': 'open', 'pid': process.pid, 'configuration_unchanged': True, 'panels_opened_explicitly': True})
        return process, folder
    def close_editor(editor):
        process, folder = editor
        (folder / 'close.request').write_text('close')
        if process.wait(timeout=8):
            raise RuntimeError('Editor failed to close normally')
        lifecycle = json.loads((folder / 'lifecycle.json').read_text())
        if lifecycle['close_events'] < 1 or lifecycle['visible_after_event_loop']:
            raise RuntimeError('MainWindow close was not completed')
        events.append({'action': 'close', 'pid': process.pid, 'normal_close_event': True})
    try:
        wait(Path(str(prefix) + '.ready'))
        first = open_editor(1)
        mark('.opened')
        wait(Path(str(prefix) + '.close-request'))
        close_editor(first)
        mark('.closed')
        wait(Path(str(prefix) + '.reopen-request'))
        second = open_editor(2)
        mark('.reopened')
        wait(Path(str(prefix) + '.final-close-request'))
        close_editor(second)
        mark('.final-closed')
        if broker.wait(timeout=50):
            raise RuntimeError((case / 'broker.log').read_text())
        result = {'status': 'PASS', 'events': events, 'restored_stream_rates': [44100, 48000], 'full_vst_states': 3, 'limits': 'Actual candidate Editor processes and private DSP streams; no installed audiodg or media-player playback'}
    except Exception as error:
        result = {'status': 'FAIL', 'error': str(error), 'events': events}
    finally:
        for process, folder in editors:
            if process.poll() is None:
                (folder / 'close.request').write_text('close')
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=5)
        if broker.poll() is None:
            broker.terminate()
            broker.wait(timeout=5)
(case / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(result['status'], case)
raise SystemExit(result['status'] != 'PASS')
