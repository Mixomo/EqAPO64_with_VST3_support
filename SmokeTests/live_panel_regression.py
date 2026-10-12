"""Opt-in installed-device bypass/panel regression; explicit paths and endpoints."""
import argparse, ctypes as C, json, math, re, subprocess, time, uuid
from ctypes import wintypes as W
from pathlib import Path
import numpy as np
import sounddevice as sd
import hashlib

U=C.WinDLL('user32',use_last_error=True)
K=C.WinDLL('kernel32',use_last_error=True)
for name,args,result in [
    ('GetWindowThreadProcessId',[W.HWND,C.POINTER(W.DWORD)],W.DWORD),
    ('GetWindowTextW',[W.HWND,W.LPWSTR,C.c_int],C.c_int),
    ('IsWindow',[W.HWND],W.BOOL),('IsWindowVisible',[W.HWND],W.BOOL),
    ('SetForegroundWindow',[W.HWND],W.BOOL),('PostMessageW',[W.HWND,W.UINT,W.WPARAM,W.LPARAM],W.BOOL),
    ('EnumWindows',[C.c_void_p,W.LPARAM],W.BOOL),('EnumChildWindows',[W.HWND,C.c_void_p,W.LPARAM],W.BOOL)]:
    function=getattr(U,name);function.argtypes=args;function.restype=result
for name,args,result in [('OpenFileMappingW',[W.DWORD,W.BOOL,W.LPCWSTR],C.c_void_p),
    ('OpenProcess',[W.DWORD,W.BOOL,W.DWORD],C.c_void_p),
    ('QueryFullProcessImageNameW',[C.c_void_p,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)],W.BOOL),
    ('MapViewOfFile',[C.c_void_p,W.DWORD,W.DWORD,W.DWORD,C.c_size_t],C.c_void_p),
    ('UnmapViewOfFile',[C.c_void_p],W.BOOL),('CloseHandle',[C.c_void_p],W.BOOL)]:
    function=getattr(K,name);function.argtypes=args;function.restype=result
CALLBACK=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)

def process_path(pid):
    handle=K.OpenProcess(0x1000,False,pid)
    if not handle:raise RuntimeError('WORKER_PATH_ACCESS')
    try:
        text=C.create_unicode_buffer(32768);length=W.DWORD(32768)
        if not K.QueryFullProcessImageNameW(handle,0,text,C.byref(length)):raise RuntimeError('WORKER_PATH_QUERY')
        return Path(text.value)
    finally:K.CloseHandle(handle)

def windows(pid=None):
    found=[]
    @CALLBACK
    def visit(hwnd,unused):
        owner=W.DWORD();U.GetWindowThreadProcessId(hwnd,C.byref(owner))
        title=C.create_unicode_buffer(512);U.GetWindowTextW(hwnd,title,512)
        if pid is None or owner.value==pid:
            found.append(dict(hwnd=int(hwnd),pid=owner.value,title=title.value,visible=bool(U.IsWindowVisible(hwnd))))
        return True
    U.EnumWindows(visit,0)
    return found

def meter(hwnd):
    texts=[]
    @CALLBACK
    def visit(child,unused):
        title=C.create_unicode_buffer(256);U.GetWindowTextW(child,title,256)
        if title.value.startswith('Fixture actual output RMS'):texts.append(title.value)
        return True
    if hwnd:U.EnumChildWindows(hwnd,visit,0)
    return texts

def mapping(name,size):
    handle=K.OpenFileMappingW(4,False,name)
    if not handle:return None
    address=K.MapViewOfFile(handle,4,0,0,size)
    raw=C.string_at(address,size) if address else None
    if address:K.UnmapViewOfFile(address)
    K.CloseHandle(handle);return raw

def view(session,sid,retained=0):
    import struct
    for namespace in ('Global\\','Local\\'):
        prefix=namespace+'EqApoControl_'+session+'_'+sid
        raw=mapping(prefix+'_Current',32)
        generation=0
        if raw:
            magic,version,identity,active,pending=struct.unpack('<IIQQQ',raw)
            if magic!=0x434f5045 or version!=2:continue
            generation=active or pending
        for target in dict.fromkeys([generation,retained]):
            if not target:continue
            raw=mapping(prefix+'_'+str(target)+'_View',48)
            if not raw:continue
            values=struct.unpack_from('<IIQQIIIII?',raw)
            if values[0]!=0x564e5045 or values[1]!=4:continue
            result=dict(zip(('magic','version','generation','identity','command','status','pid','editor_pid','visibility','inherited'),values))
            if result['pid'] and (result['status'] in (1,2,3,6) or target==retained):return result
    return {}

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--editor',type=Path,required=True)
    p.add_argument('--config',type=Path,required=True)
    p.add_argument('--case-file',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--render-device',type=int,required=True)
    p.add_argument('--loopback-device',type=int,required=True)
    p.add_argument('--sample-rate',type=int,default=44100)
    p.add_argument('--seconds',type=int,default=60)
    a=p.parse_args()
    if not 30<=a.seconds<=60:p.error('--seconds must be 30–60')
    case=json.loads(a.case_file.read_text(encoding='utf-8-sig'))
    if '<' in case['line'] or not a.editor.is_file():p.error('Complete the external settings')
    a.output.mkdir(parents=True,exist_ok=False)
    original=a.config.read_bytes();session=str(uuid.uuid4())
    line=re.sub(r'^\s*#\s*','',case['line'].strip())
    line=re.sub(r'HostId\s+\S+', 'HostId '+session,line)
    if 'HostId ' not in line:line+=' HostId '+session
    sid=case['sid'];assert re.fullmatch(r'S-1-5-21-[0-9-]+',sid)
    if 'GuiUserSID ' not in line:line+=' GuiUserSID '+sid
    staged=('Preamp: 0 dB\n'+line+'\n').encode('utf-8')
    editor=None;frames=0;capture=[];reference=[];events=[];observations=[];retained=0;main_hwnd=0
    report=dict(status='FAIL',case=case['name'])
    start=time.monotonic();deadline=start+a.seconds
    def check(condition,message):
        if not condition:raise RuntimeError(message)
    def hold(seconds,label):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            check(time.monotonic()<deadline,'CASE_DEADLINE')
            state=view(session,sid,retained)
            observations.append(dict(time=time.monotonic()-start,phase=label,view=state))
            time.sleep(.1)
    def invoke(action,gain=0):
        result=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(Path(__file__).with_name('invoke-live-editor.ps1')),
            '-EditorPID',str(editor.pid),'-EditorPath',str(a.editor.resolve()),'-Action',action,'-Gain',str(gain)],capture_output=True,text=True,timeout=6)
        events.append(dict(action=action,gain=gain,time=time.monotonic()-start,exit=result.returncode,stdout=result.stdout,stderr=result.stderr))
        check(result.returncode==0,'EDITOR_UIA_'+action+': '+result.stderr[-400:])
        return json.loads(result.stdout)
    def current_panel(expected_visible=True):
        state=view(session,sid,retained)
        candidates=[w for w in windows(state.get('pid',0)) if w['title']=='Equalizer APO - VST']
        check(len(candidates)==1,'EXPECTED_ONE_NATIVE_PANEL: '+str(candidates))
        check(candidates[0]['visible']==expected_visible,'PANEL_VISIBILITY')
        return state,candidates[0]['hwnd']
    def callback(indata,outdata,count,ti,status):
        nonlocal frames
        signal=(.003*np.sin(2*np.pi*997*(np.arange(count)+frames)/a.sample_rate)).astype(np.float32)
        outdata[:]=signal[:,None];capture.append(indata.copy());reference.append(signal);frames+=count
        if status:events.append(dict(action='stream-status',message=str(status),time=time.monotonic()-start))
    try:
        with a.config.open('r+b') as file:file.write(staged);file.truncate();file.flush()
        editor=subprocess.Popen([str(a.editor.resolve()),str(a.config.resolve())])
        with sd.Stream(device=(a.loopback_device,a.render_device),samplerate=a.sample_rate,channels=2,dtype='float32',blocksize=441,callback=callback):
            ready=None
            for _ in range(100):
                check(time.monotonic()<deadline,'OWNED_EDITOR_STARTUP_DEADLINE')
                candidates=[w for w in windows(editor.pid) if w['visible'] and ('Configuration Editor' in w['title'] or 'Editor' in w['title'])]
                if candidates:
                    main_hwnd=candidates[0]['hwnd']
                    try:ready=invoke('inspect');main_hwnd=ready['editor_hwnd'];break
                    except RuntimeError:pass
                check(editor.poll() is None,'OWNED_EDITOR_EXITED');time.sleep(.1)
            check(ready is not None,'OWNED_EDITOR_NOT_READY')
            U.SetForegroundWindow(main_hwnd);invoke('panel')
            for _ in range(120):
                state=view(session,sid)
                if state.get('status')==2:break
                check(time.monotonic()<deadline,'NATIVE_PANEL_STARTUP_DEADLINE');time.sleep(.1)
            check(state.get('status')==2,'NO_LIVE_NATIVE_PANEL')
            retained=state['generation'];pid=state['pid'];state,hwnd=current_panel()
            worker_path=process_path(pid)
            if case.get('expected_worker_path'):
                check(worker_path.resolve()==Path(case['expected_worker_path']).resolve(),'UNEXPECTED_WORKER_BINARY')
            report.update(worker_executable=str(worker_path),worker_sha256=hashlib.sha256(worker_path.read_bytes()).hexdigest())
            rank=[w['hwnd'] for w in windows()]
            check(rank.index(hwnd)<rank.index(main_hwnd),'VST_PANEL_BEHIND_EDITOR')
            hold(2,'wet-before');before_meter=meter(hwnd)
            invoke('power');hold(1,'dry-visible');state,kept=current_panel();check(kept==hwnd and state['pid']==pid,'BYPASS_CHANGED_NATIVE_WINDOW')
            silent_meter=meter(hwnd)
            invoke('panel');hold(.3,'dry-hidden');current_panel(False)
            for gain in (-1,-2,0):invoke('preamp',gain);hold(.3,'dry-churn')
            hold(5.2,'dry-hidden-retained');state,kept=current_panel(False);check(kept==hwnd and state['pid']==pid,'HIDDEN_PANEL_EXPIRED_DURING_BYPASS')
            invoke('panel');hold(.4,'dry-visible');state,kept=current_panel();check(kept==hwnd,'SHOW_CREATED_DUPLICATE_PANEL')
            U.PostMessageW(hwnd,0x10,0,0);hold(.5,'dry-closed')
            check(not U.IsWindow(hwnd),'X_LEFT_ORPHAN_WINDOW')
            check(invoke('inspect')['panel_name']=='Open panel','X_DID_NOT_RESET_PANEL_BUTTON')
            invoke('panel');hold(.6,'dry-reopened');state,reopened=current_panel();check(state['pid']==pid,'REOPEN_DURING_BYPASS_CHANGED_WORKER')
            invoke('power');hold(2,'wet-restored');state,kept=current_panel();retained=state['generation'];check(state['pid']==pid and kept==reopened,'REACTIVATION_CHANGED_RETAINED_GUI')
            restored_meter=meter(kept)
            invoke('panel');hold(.3,'wet-hidden');current_panel(False)
            invoke('power');hold(2,'dry-hidden-second');current_panel(False)
            invoke('power');hold(2,'wet-hidden-restored');state,kept=current_panel(False);retained=state['generation']
            check(state['pid']==pid and kept==reopened,'HIDDEN_REACTIVATION_CREATED_NEW_GUI')
            invoke('panel');hold(1,'wet-visible-final');current_panel()
            U.PostMessageW(kept,0x10,0,0);hold(.5,'wet-X-closed')
            check(not U.IsWindow(kept),'ACTIVE_X_LEFT_ORPHAN')
            check(invoke('inspect')['panel_name']=='Open panel','ACTIVE_X_BUTTON_STATE')
            report.update(status='PASS',same_worker=pid,first_hwnd=hwnd,reopened_hwnd=reopened,
                meter_before=before_meter,meter_during_bypass=silent_meter,meter_after_reactivation=restored_meter)
            if before_meter:
                check(any(float(v.rsplit(' ',1)[1])>0 for v in before_meter),'FIXTURE_METER_NO_LIVE_INPUT')
                check(all(float(v.rsplit(' ',1)[1])<1e-5 for v in silent_meter) and bool(silent_meter),'FIXTURE_METER_FROZEN_DURING_BYPASS')
                check(any(float(v.rsplit(' ',1)[1])>0 for v in restored_meter),'FIXTURE_METER_NOT_RESTORED')
    except Exception as error:report.update(status='FAIL',reason=str(error))
    finally:
        if editor and editor.poll() is None:
            if main_hwnd:U.PostMessageW(main_hwnd,0x10,0,0)
            try:editor.wait(timeout=5)
            except subprocess.TimeoutExpired:editor.terminate();editor.wait(timeout=3)
        # This is the explicit temporary test config, never the user's source config.
        with a.config.open('r+b') as file:file.write(original);file.truncate();file.flush()
        signal=np.concatenate(capture) if capture else np.empty((0,2))
        source=np.concatenate(reference) if reference else np.empty(0)
        np.savez(a.output/'signal.npz',capture=signal,reference=source,rate=a.sample_rate)
        report.update(events=events,observations=observations,elapsed=time.monotonic()-start,finite=bool(np.all(np.isfinite(signal))),config_restored=a.config.read_bytes()==original)
        if not report['finite']:report.update(status='FAIL',reason='NONFINITE_LOOPBACK')
        phases={}
        for label in ('wet-before','dry-visible','wet-restored','wet-hidden-restored','wet-visible-final'):
            times=[row['time'] for row in observations if row['phase']==label]
            if times:
                # Repeated phases may surround unrelated Preamp changes.
                # Measure only their contiguous settled intervals.
                intervals=[];first=last=times[0]
                for current in times[1:]:
                    if current-last>.25:intervals.append((first,last));first=current
                    last=current
                intervals.append((first,last))
                pieces=[signal[int((first+.4)*a.sample_rate):int(last*a.sample_rate)] for first,last in intervals if last-first>.4]
                sample=np.concatenate(pieces) if pieces else np.empty((0,2))
                if len(sample):phases[label]=np.sqrt(np.mean(sample.astype(float)**2,axis=0)).tolist()
        report['rms_by_phase']=phases
        if report['status']=='PASS':
            if not phases or any(max(v)<1e-7 for v in phases.values()):report.update(status='FAIL',reason='MISSING_REAL_LOOPBACK_AUDIO')
            if case.get('expected_gain') is not None and 'wet-before' in phases and 'dry-visible' in phases:
                ratio=phases['wet-before'][0]/phases['dry-visible'][0];report['measured_wet_dry_ratio']=ratio
                if abs(ratio/case['expected_gain']-1)>.08:report.update(status='FAIL',reason='REAL_WET_DRY_GAIN_ORACLE')
        (a.output/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(json.dumps({key:report.get(key) for key in ('status','reason','elapsed','same_worker','measured_wet_dry_ratio','config_restored')}),flush=True)
    return 0 if report['status']=='PASS' else 1

if __name__=='__main__':raise SystemExit(main())
