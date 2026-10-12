"""Exercise the real GUI child and generation/state publication, without installing an APO."""
import argparse, ctypes as c, json, os, struct, subprocess, time
from ctypes import wintypes as w
from pathlib import Path

def string(value):
    return struct.pack('<I',len(value))+value.encode('utf-16-le')

def read_parameters(path):
    data=path.read_bytes();offset=8
    def text():
        nonlocal offset
        size=struct.unpack_from('<I',data,offset)[0];offset+=4
        value=data[offset:offset+size*2].decode('utf-16-le');offset+=size*2
        return value
    text();offset+=4;text();text()
    count=struct.unpack_from('<I',data,offset)[0];offset+=4;parameters={}
    for _ in range(count):
        key=text();parameters[key]=struct.unpack_from('<f',data,offset)[0];offset+=4
    return parameters

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',type=Path,required=True);p.add_argument('--plugin',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);config=(args.output/'gui.opvs').resolve()
    config.write_bytes(struct.pack('<II',0x4f505653,3)+string(str(args.plugin.resolve()))+struct.pack('<i',0)+string('')+string('')+struct.pack('<I',1)+string('Gain')+struct.pack('<f',1.0))
    kernel=c.WinDLL('kernel32',use_last_error=True);user=c.WinDLL('user32',use_last_error=True)
    kernel.OpenFileMappingW.argtypes=[w.DWORD,w.BOOL,w.LPCWSTR];kernel.OpenFileMappingW.restype=w.HANDLE
    kernel.MapViewOfFile.argtypes=[w.HANDLE,w.DWORD,w.DWORD,w.DWORD,c.c_size_t];kernel.MapViewOfFile.restype=c.c_void_p
    kernel.UnmapViewOfFile.argtypes=[c.c_void_p];kernel.CloseHandle.argtypes=[w.HANDLE]
    kernel.OpenEventW.argtypes=[w.DWORD,w.BOOL,w.LPCWSTR];kernel.OpenEventW.restype=w.HANDLE;kernel.SetEvent.argtypes=[w.HANDLE]
    callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
    user.EnumWindows.argtypes=[callback,w.LPARAM];user.EnumChildWindows.argtypes=[w.HWND,callback,w.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)];user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
    user.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,c.POINTER(c.c_size_t)]
    session='state-regression-'+str(os.getpid());prefix='Local\\EqApoOutProcVST_'+session+'_'
    mapping=None;view=None;started=time.monotonic();result={'status':'FAIL','limits':'GUI generation and persisted parameter transport only; this test does not prove active DSP, meters or installed audiodg.'}
    with (args.output/'host.log').open('wb') as log:
        child=subprocess.Popen([str(args.host.resolve()),'--gui','--vst-config',str(config),'--session',session,'--parent-pid',str(os.getpid())],stdout=log,stderr=subprocess.STDOUT)
        try:
            while time.monotonic()-started<10 and child.poll() is None:
                mapping=kernel.OpenFileMappingW(4,False,prefix+'GuiInfo')
                if mapping:break
                time.sleep(.02)
            if not mapping:raise RuntimeError('GUI_MAPPING_UNAVAILABLE')
            view=kernel.MapViewOfFile(mapping,4,0,0,24)
            if not view:raise RuntimeError('GUI_MAPPING_VIEW')
            found=[]
            @callback
            def visit(window,_):
                name=c.create_unicode_buffer(128);user.GetClassNameW(window,name,len(name))
                if name.value=='EqApoFixtureControl':found.append(window)
                return True
            @callback
            def top(window,_):
                pid=w.DWORD();user.GetWindowThreadProcessId(window,c.byref(pid))
                if pid.value==child.pid:user.EnumChildWindows(window,visit,0)
                return True
            while not found and time.monotonic()-started<10:
                user.EnumWindows(top,0);time.sleep(.02)
            if not found:raise RuntimeError('FIXTURE_EDITOR_UNAVAILABLE')
            before=struct.unpack('<Q',c.string_at(view+16,8))[0];answer=c.c_size_t()
            if not user.SendMessageTimeoutW(found[0],0x202,0,0,2,1000,c.byref(answer)):raise RuntimeError('GUI_EDIT_TIMEOUT')
            changed=None
            while time.monotonic()-started<15:
                generation=struct.unpack('<Q',c.string_at(view+16,8))[0]
                if generation>before:
                    changed=read_parameters(config)
                    if changed.get('Gain')==.25:break
                time.sleep(.02)
            if not changed or changed.get('Gain')!=.25:raise RuntimeError('GUI_PARAMETER_NOT_PUBLISHED')
            result.update(status='PASS',before_generation=before,after_generation=generation,parameters=changed)
        except Exception as error:result['reason']=str(error)
        finally:
            event=kernel.OpenEventW(2,False,prefix+'GuiExit')
            if event:kernel.SetEvent(event);kernel.CloseHandle(event)
            try:child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                child.kill();child.wait();result['cleanup_timeout']=True
                if result['status']=='PASS':result.update(status='FAIL',reason='GUI_SHUTDOWN_TIMEOUT')
            if view:kernel.UnmapViewOfFile(view)
            if mapping:kernel.CloseHandle(mapping)
    result['seconds']=round(time.monotonic()-started,3);result['host_exit']=child.returncode
    if child.returncode and result['status']=='PASS':result.update(status='FAIL',reason='HOST_EXIT_'+str(child.returncode))
    (args.output/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
    if result['status']!='PASS':raise SystemExit(1)

if __name__=='__main__':main()
