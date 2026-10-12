"""Owned real UI gestures, snapshots and PCM observations; explicit oracle required."""
import argparse,ctypes as c,hashlib,json,os,subprocess,time
from ctypes import wintypes as w
from pathlib import Path
from test_native_panel import capture

def main():
    p=argparse.ArgumentParser();p.add_argument('--editor',type=Path,required=True);p.add_argument('--plugin',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--mode',choices=['INPROC','OUTPROC'],required=True)
    p.add_argument('--seconds',type=int,default=25);p.add_argument('--click',nargs=3,type=float,action='append',default=[])
    p.add_argument('--region',nargs=4,type=int);args=p.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    user=c.WinDLL('user32');user.SetProcessDpiAwarenessContext.argtypes=[c.c_void_p];user.SetProcessDpiAwarenessContext(c.c_void_p(-4))
    user.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)];user.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
    user.GetAncestor.argtypes=[w.HWND,w.UINT];user.GetAncestor.restype=w.HWND
    user.SetWindowPos.argtypes=[w.HWND,w.HWND,c.c_int,c.c_int,c.c_int,c.c_int,w.UINT];user.SetForegroundWindow.argtypes=[w.HWND];user.IsWindow.argtypes=[w.HWND];user.SetCursorPos.argtypes=[c.c_int,c.c_int]
    user.GetCursorPos.argtypes=[c.POINTER(w.POINT)];user.WindowFromPoint.argtypes=[w.POINT];user.WindowFromPoint.restype=w.HWND
    user.mouse_event.argtypes=[w.DWORD,w.DWORD,w.DWORD,w.DWORD,c.c_size_t]
    began=time.monotonic();events=[];shots=[];child=None
    try:
        with (args.output/'console.log').open('wb') as log:
            child=subprocess.Popen([str(args.editor.resolve()),'--commercial-gui-audit',str(args.plugin.resolve()),str(args.output.resolve()),args.mode,str(args.seconds)],stdout=log,stderr=subprocess.STDOUT)
            panel=args.output/'panel.json'
            while not panel.exists() and child.poll() is None:
                if time.monotonic()-began>20:raise RuntimeError('Panel not published within bound')
                time.sleep(.05)
            info=json.loads(panel.read_text());hwnd=w.HWND(int(info['hwnd']));owned=info['pid'];ready=time.monotonic()
            clicks=sorted(args.click);snapshots=iter([0,1,3,6,10,15]);next_shot=next(snapshots,None)
            while child.poll() is None:
                now=time.monotonic()-ready
                current=json.loads(panel.read_text())
                hwnd=w.HWND(int(current['hwnd']));owned=current['pid']
                if time.monotonic()-began>54:raise RuntimeError('Owned smoke exceeded time cap')
                if next_shot is not None and now>=next_shot and user.IsWindow(hwnd):
                    path=args.output/('native-'+str(next_shot)+'.png');capture(int(hwnd.value),path)
                    shots.append({'seconds_after_panel':next_shot,'file':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()});next_shot=next(snapshots,None)
                if clicks and now>=clicks[0][0]:
                    at,x,y=clicks.pop(0);rect=w.RECT();user.GetWindowRect(hwnd,c.byref(rect));user.SetForegroundWindow(user.GetAncestor(hwnd,2))
                    old=w.POINT();user.GetCursorPos(c.byref(old));point=w.POINT(rect.left+int(x),rect.top+int(y))
                    try:
                        user.SetWindowPos(user.GetAncestor(hwnd,2),w.HWND(-1),0,0,0,0,3);user.SetForegroundWindow(user.GetAncestor(hwnd,2));user.SetCursorPos(point.x,point.y);time.sleep(.2);target=user.WindowFromPoint(point);pid=w.DWORD();user.GetWindowThreadProcessId(target,c.byref(pid))
                        if pid.value!=owned:raise RuntimeError('Gesture target not the owned VST panel')
                        user.mouse_event(2,0,0,0,0);time.sleep(.05);user.mouse_event(4,0,0,0,0)
                        events.append({'seconds_after_panel':at,'point':[x,y],'target_pid':pid.value})
                    finally:
                        user.SetWindowPos(user.GetAncestor(hwnd,2),w.HWND(-2),0,0,0,0,3);user.SetCursorPos(old.x,old.y)
                time.sleep(.04)
            report=args.output/'result.json'
            result=json.loads(report.read_text()) if report.exists() else {'status':'HARNESS_EXIT_WITHOUT_REPORT','limits':'Owned process exited before final PCM report; no success claim'}
            result.update(exit_code=child.returncode,gestures=events,snapshots=shots,elapsed_seconds=time.monotonic()-began)
            if args.region:
                from PIL import Image,ImageChops
                images=[Image.open(args.output/s['file']).convert('RGB').crop(tuple(args.region)) for s in shots]
                result['declared_region']=args.region;result['changed_region_pairs']=sum(ImageChops.difference(a,b).getbbox() is not None for a,b in zip(images,images[1:]))
            (args.output/'observations.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:result[k] for k in ['status','exit_code','elapsed_seconds','graph_reloads','model_updates','changed_region_pairs'] if k in result}))
            return 0 if child.returncode==0 else 1
    finally:
        if child and child.poll() is None:
            subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],capture_output=True,timeout=5)
            child.wait(timeout=5)
if __name__=='__main__':raise SystemExit(main())
