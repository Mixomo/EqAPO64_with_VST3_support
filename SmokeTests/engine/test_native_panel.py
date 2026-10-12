"""Capture and close a real native panel in a sacrificial host; no installed APO changes."""
import argparse,ctypes as c,json,os,struct,subprocess,time,sys
from ctypes import wintypes as w
from pathlib import Path
from PIL import Image
from test_gui_state_bridge import string,read_parameters

def capture_window(hwnd,path):
    user=c.WinDLL('user32');gdi=c.WinDLL('gdi32');rect=w.RECT()
    user.SetProcessDpiAwarenessContext.argtypes=[c.c_void_p];user.SetProcessDpiAwarenessContext(c.c_void_p(-4))
    user.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)];user.GetWindowRect(hwnd,c.byref(rect));width,height=rect.right-rect.left,rect.bottom-rect.top
    user.GetWindowDC.argtypes=[w.HWND];user.GetWindowDC.restype=w.HDC;user.ReleaseDC.argtypes=[w.HWND,w.HDC]
    gdi.CreateCompatibleDC.argtypes=[w.HDC];gdi.CreateCompatibleDC.restype=w.HDC
    gdi.CreateCompatibleBitmap.argtypes=[w.HDC,c.c_int,c.c_int];gdi.CreateCompatibleBitmap.restype=w.HANDLE
    gdi.SelectObject.argtypes=[w.HDC,w.HANDLE];gdi.SelectObject.restype=w.HANDLE;gdi.DeleteObject.argtypes=[w.HANDLE];gdi.DeleteDC.argtypes=[w.HDC]
    user.PrintWindow.argtypes=[w.HWND,w.HDC,w.UINT];user.PrintWindow.restype=w.BOOL
    gdi.GetDIBits.argtypes=[w.HDC,w.HANDLE,w.UINT,w.UINT,c.c_void_p,c.c_void_p,w.UINT]
    gdi.BitBlt.argtypes=[w.HDC,c.c_int,c.c_int,c.c_int,c.c_int,w.HDC,c.c_int,c.c_int,w.DWORD]
    dc=user.GetWindowDC(hwnd);memory=gdi.CreateCompatibleDC(dc);bitmap=gdi.CreateCompatibleBitmap(dc,width,height);previous=gdi.SelectObject(memory,bitmap)
    try:
        method='PrintWindow'
        if not user.PrintWindow(hwnd,memory,2):
            method='BitBlt owned window DC'
            if not gdi.BitBlt(memory,0,0,width,height,dc,0,0,0x40CC0020):raise RuntimeError('OWNED_WINDOW_CAPTURE_FAILED')
        gdi.SelectObject(memory,previous);previous=None
        info=struct.pack('<IiiHHIIiiII',40,width,-height,1,32,0,width*height*4,0,0,0,0)
        header=c.create_string_buffer(info);pixels=c.create_string_buffer(width*height*4)
        if gdi.GetDIBits(dc,bitmap,0,height,pixels,header,0)!=height:raise RuntimeError('NATIVE_CAPTURE_FAILED')
        Image.frombuffer('RGB',(width,height),pixels.raw,'raw','BGRX',0,1).save(path)
        Path(str(path)+'.capture.json').write_text(json.dumps({'method':method,'hwnd':int(hwnd),'width':width,'height':height}))
    finally:
        if previous:gdi.SelectObject(memory,previous)
        gdi.DeleteObject(bitmap);gdi.DeleteDC(memory);user.ReleaseDC(hwnd,dc)

def capture(hwnd,path):
    # PrintWindow can block in a faulty plugin callback: bound a helper process.
    subprocess.run([sys.executable,__file__,'--capture-window',str(hwnd),str(path)],check=True,timeout=3)

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',type=Path,required=True);p.add_argument('--plugin',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--click',nargs=2,type=int);p.add_argument('--session');p.add_argument('--wait-before-click',type=Path);p.add_argument('--hold-for-report',type=Path);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    config=(args.output/'panel.opvs').resolve();config.write_bytes(struct.pack('<II',0x4f505653,3)+string(str(args.plugin.resolve()))+struct.pack('<i',0)+string('')+string('')+struct.pack('<I',0))
    kernel=c.WinDLL('kernel32',use_last_error=True);user=c.WinDLL('user32',use_last_error=True)
    user.SetProcessDpiAwarenessContext.argtypes=[c.c_void_p];user.SetProcessDpiAwarenessContext(c.c_void_p(-4))
    callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
    user.EnumWindows.argtypes=[callback,w.LPARAM];user.EnumChildWindows.argtypes=[w.HWND,callback,w.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)];user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,c.c_int];user.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
    user.SetForegroundWindow.argtypes=[w.HWND];user.IsWindowVisible.argtypes=[w.HWND];user.ScreenToClient.argtypes=[w.HWND,c.POINTER(w.POINT)]
    user.SetWindowPos.argtypes=[w.HWND,w.HWND,c.c_int,c.c_int,c.c_int,c.c_int,w.UINT]
    user.SetCursorPos.argtypes=[c.c_int,c.c_int];user.GetCursorPos.argtypes=[c.POINTER(w.POINT)]
    user.WindowFromPoint.argtypes=[w.POINT];user.WindowFromPoint.restype=w.HWND
    user.GetDpiForWindow.argtypes=[w.HWND];user.GetDpiForWindow.restype=w.UINT
    user.GetWindowDpiAwarenessContext.argtypes=[w.HWND];user.GetWindowDpiAwarenessContext.restype=c.c_void_p
    user.GetAwarenessFromDpiAwarenessContext.argtypes=[c.c_void_p];user.GetAwarenessFromDpiAwarenessContext.restype=c.c_int
    user.mouse_event.argtypes=[w.DWORD,w.DWORD,w.DWORD,w.DWORD,c.c_size_t]
    user.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,c.POINTER(c.c_size_t)]
    kernel.OpenEventW.argtypes=[w.DWORD,w.BOOL,w.LPCWSTR];kernel.OpenEventW.restype=w.HANDLE;kernel.SetEvent.argtypes=[w.HANDLE];kernel.CloseHandle.argtypes=[w.HANDLE]
    session=args.session or 'native-panel-'+str(os.getpid());start=time.monotonic();result={'status':'FAIL','limits':'Sacrificial native GUI host; paired PCM report can prove concurrent DSP; no installed audiodg, meter animation or Qt close/freeze certification.'};window=[]
    with (args.output/'console.log').open('wb') as log:
        child=subprocess.Popen([str(args.host.resolve()),'--gui','--vst-config',str(config),'--session',session,'--parent-pid',str(os.getpid())],stdout=log,stderr=subprocess.STDOUT)
        try:
            @callback
            def top(hwnd,_):
                pid=w.DWORD();name=c.create_unicode_buffer(128);user.GetWindowThreadProcessId(hwnd,c.byref(pid));user.GetClassNameW(hwnd,name,len(name))
                if pid.value==child.pid and name.value=='EqApoOutProcVSTGuiWindow' and user.IsWindowVisible(hwnd):window.append(hwnd)
                return True
            while not window and time.monotonic()-start<15 and child.poll() is None:user.EnumWindows(top,0);time.sleep(.05)
            if not window:raise RuntimeError('NATIVE_PANEL_UNAVAILABLE')
            hwnd=window[0];user.SetForegroundWindow(hwnd);time.sleep(.5);rect=w.RECT();user.GetWindowRect(hwnd,c.byref(rect))
            bounds=(rect.left,rect.top,rect.right,rect.bottom);capture(hwnd,args.output/'before.png')
            before=read_parameters(config);(args.output/'before.opvs').write_bytes(config.read_bytes());result['geometry']=list(bounds)
            result['window_dpi']=user.GetDpiForWindow(hwnd);result['window_awareness']=user.GetAwarenessFromDpiAwarenessContext(user.GetWindowDpiAwarenessContext(hwnd))
            if args.click:
                if args.wait_before_click:
                    deadline=time.monotonic()+10
                    while not args.wait_before_click.exists() and time.monotonic()<deadline:time.sleep(.05)
                    if not args.wait_before_click.exists():raise RuntimeError('PCM_BASELINE_NOT_READY')
                px,py=rect.left+args.click[0],rect.top+args.click[1];targets=[hwnd]
                @callback
                def visit(target,_):
                    area=w.RECT();user.GetWindowRect(target,c.byref(area))
                    if user.IsWindowVisible(target) and area.left<=px<area.right and area.top<=py<area.bottom:targets.append(target)
                    return True
                user.EnumChildWindows(hwnd,visit,0)
                previous=w.POINT();user.GetCursorPos(c.byref(previous))
                user.SetWindowPos(hwnd,w.HWND(-1),0,0,0,0,0x53)
                user.SetCursorPos(px,py);time.sleep(.05)
                target=user.WindowFromPoint(w.POINT(px,py));pid=w.DWORD();user.GetWindowThreadProcessId(target,c.byref(pid))
                if pid.value!=child.pid:
                    user.SetCursorPos(previous.x,previous.y);raise RuntimeError('CLICK_TARGET_NOT_OWN_HOST')
                try:
                    user.mouse_event(2,0,0,0,0);time.sleep(.05);user.mouse_event(4,0,0,0,0)
                    # Let the plugin process mouse-up before moving the cursor
                    # back; otherwise a slow UI may interpret restoration as drag.
                    time.sleep(.2)
                finally:user.SetCursorPos(previous.x,previous.y)
                time.sleep(.5)
            after=read_parameters(config);(args.output/'after.opvs').write_bytes(config.read_bytes());result['parameter_changes']={key:{'before':before.get(key),'after':value} for key,value in after.items() if value!=before.get(key)}
            capture(hwnd,args.output/'after.png');result['status']='PASS'
            if args.hold_for_report:
                deadline=time.monotonic()+15
                while not args.hold_for_report.exists() and time.monotonic()<deadline:
                    if child.poll() is not None or not user.IsWindowVisible(hwnd):raise RuntimeError('GUI_NOT_ALIVE_DURING_PCM')
                    time.sleep(.05)
                if not args.hold_for_report.exists():raise RuntimeError('PAIRED_PCM_REPORT_TIMEOUT')
                result['gui_visible_through_pcm_report']=bool(user.IsWindowVisible(hwnd))
        except Exception as error:result['reason']=str(error)
        finally:
            close=time.monotonic();event=kernel.OpenEventW(2,False,'Local\\EqApoOutProcVST_'+session+'_GuiExit')
            if event:kernel.SetEvent(event);kernel.CloseHandle(event)
            try:child.wait(timeout=5)
            except subprocess.TimeoutExpired:child.kill();child.wait();result.update(status='GUI_FAILURE',reason='NATIVE_CLOSE_TIMEOUT')
            result['close_seconds']=round(time.monotonic()-close,3);result['host_exit']=child.returncode
    result['seconds']=round(time.monotonic()-start,3)
    if child.returncode and result['status']=='PASS':result.update(status='GUI_FAILURE',reason='HOST_EXIT_'+str(child.returncode))
    (args.output/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
    if result['status']!='PASS':raise SystemExit(1)

if __name__=='__main__':
    if len(sys.argv)>1 and sys.argv[1]=='--capture-window':capture_window(int(sys.argv[2]),sys.argv[3])
    else:main()
