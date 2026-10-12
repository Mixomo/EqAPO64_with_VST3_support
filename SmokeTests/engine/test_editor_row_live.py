"""Click an owned native panel embedded in the real Editor row/dialog."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--editor', type=Path, required=True)
    parser.add_argument('--plugin', type=Path, required=True)
    parser.add_argument('--id', type=int, required=True)
    parser.add_argument('--before', type=float, required=True)
    parser.add_argument('--ratio', type=float, required=True)
    parser.add_argument('--click', nargs=2, type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    panel_path = args.output / 'panel.json'
    if panel_path.exists() or (args.output / 'result.json').exists():
        raise RuntimeError('Fresh output directory required')
    user = c.WinDLL('user32', use_last_error=True)
    user.SetProcessDpiAwarenessContext.argtypes = [c.c_void_p]
    user.SetProcessDpiAwarenessContext(c.c_void_p(-4))
    user.GetAncestor.argtypes = [w.HWND, w.UINT]
    user.GetAncestor.restype = w.HWND
    user.SetForegroundWindow.argtypes = [w.HWND]
    user.BringWindowToTop.argtypes = [w.HWND]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.IsIconic.argtypes = [w.HWND]
    user.GetWindowLongPtrW.argtypes = [w.HWND, c.c_int]
    user.GetWindowLongPtrW.restype = c.c_ssize_t
    user.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
    user.WindowFromPoint.argtypes = [w.POINT]
    user.WindowFromPoint.restype = w.HWND
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.GetCursorPos.argtypes = [c.POINTER(w.POINT)]
    user.SetCursorPos.argtypes = [c.c_int, c.c_int]
    user.mouse_event.argtypes = [w.DWORD, w.DWORD, w.DWORD, w.DWORD, c.c_size_t]
    child = None
    started = time.monotonic()
    try:
        with (args.output / 'process.log').open('wb') as log:
            child = subprocess.Popen([str(args.editor.resolve()), '--row-live-audit',
                                      str(args.plugin.resolve()), str(args.output.resolve()),
                                      str(args.id), str(args.before), str(args.ratio)],
                                     stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 12
            while not panel_path.exists() and child.poll() is None and time.monotonic() < deadline:
                time.sleep(.03)
            if not panel_path.exists():
                raise RuntimeError('ROW_PANEL_NOT_READY')
            panel = json.loads(panel_path.read_text())
            if panel['pid'] != child.pid:
                raise RuntimeError('PANEL_PROCESS_ID_MISMATCH')
            hwnd = w.HWND(int(panel['hwnd']))
            frame_pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(frame_pid))
            if frame_pid.value != child.pid:
                raise RuntimeError('NATIVE_FRAME_NOT_OWN_EDITOR')
            root = user.GetAncestor(hwnd, 2)
            panel['root_hwnd'] = str(root)
            panel['foreground_result'] = bool(user.SetForegroundWindow(root))
            panel['position_result'] = bool(user.SetWindowPos(root, w.HWND(-1), 200, 200, 0, 0, 0x41))
            panel['position_error'] = c.get_last_error()
            user.BringWindowToTop(root)
            time.sleep(.1)
            physical = w.RECT()
            if not user.GetWindowRect(hwnd, c.byref(physical)):
                raise RuntimeError('NATIVE_FRAME_BOUNDS_UNAVAILABLE')
            panel['driver_physical_bounds'] = [physical.left, physical.top, physical.right, physical.bottom]
            panel['bounds'] = panel['driver_physical_bounds']
            panel['root_visible'] = bool(user.IsWindowVisible(root))
            panel['root_minimized'] = bool(user.IsIconic(root))
            panel['root_ex_style'] = int(user.GetWindowLongPtrW(root, -20))
            (args.output / 'click-target.json').write_text(json.dumps(panel, indent=2))
            x, y = panel['bounds'][0] + args.click[0], panel['bounds'][1] + args.click[1]
            if not (panel['bounds'][0] <= x < panel['bounds'][2] and panel['bounds'][1] <= y < panel['bounds'][3]):
                raise RuntimeError('CLICK_OUTSIDE_NATIVE_FRAME')
            previous = w.POINT()
            user.GetCursorPos(c.byref(previous))
            try:
                user.SetCursorPos(x, y)
                time.sleep(.05)
                target = user.WindowFromPoint(w.POINT(x, y))
                pid = w.DWORD()
                user.GetWindowThreadProcessId(target, c.byref(pid))
                for _ in range(20):
                    if pid.value == child.pid:
                        break
                    # Activation/stacking may still be pending. Never send
                    # input while another process occupies the requested point.
                    user.SetWindowPos(root, w.HWND(-1), 0, 0, 0, 0, 0x43)
                    user.SetForegroundWindow(root)
                    time.sleep(.03)
                    target = user.WindowFromPoint(w.POINT(x, y))
                    user.GetWindowThreadProcessId(target, c.byref(pid))
                if pid.value != child.pid:
                    actual = w.POINT()
                    user.GetCursorPos(c.byref(actual))
                    panel['requested_point'] = [x, y]
                    panel['actual_cursor'] = [actual.x, actual.y]
                    panel['target_hwnd'] = str(int(target or 0))
                    panel['target_pid'] = pid.value
                    (args.output / 'click-target.json').write_text(json.dumps(panel, indent=2))
                    from test_native_panel import capture
                    capture(root, args.output / 'rejected-target-window.png')
                    raise RuntimeError('CLICK_TARGET_NOT_OWN_EDITOR')
                user.mouse_event(2, 0, 0, 0, 0)
                time.sleep(.05)
                user.mouse_event(4, 0, 0, 0, 0)
                time.sleep(.2)
            finally:
                user.SetCursorPos(previous.x, previous.y)
            time.sleep(.4)
            from test_native_panel import capture
            capture(hwnd.value, args.output / 'native-frame-after.png')
            child.wait(timeout=max(1, 25 - (time.monotonic() - started)))
        result = json.loads((args.output / 'result.json').read_text())
        result['owned_real_mouse_click'] = True
        result['test_elapsed_seconds'] = time.monotonic() - started
        result['exit_code'] = child.returncode
        (args.output / 'bounded-result.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2))
        return 0 if child.returncode == 0 and result['status'] == 'PASS' else 1
    except (RuntimeError, subprocess.TimeoutExpired, subprocess.CalledProcessError) as error:
        result = {'status': 'GUI_FAILURE' if isinstance(error, subprocess.TimeoutExpired) else 'BLOCKED_ENVIRONMENT',
                  'reason': str(error), 'plugin': str(args.plugin.resolve()),
                  'test_elapsed_seconds': time.monotonic() - started,
                  'limits': 'Interactive test did not complete; no DSP, plugin or license causation inferred.'}
        (args.output / 'bounded-result.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2))
        return 1
    finally:
        if child and child.poll() is None:
            subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
            if child.poll() is None:
                child.kill()
            child.wait(timeout=5)


if __name__ == '__main__':
    raise SystemExit(main())
