#pragma once
#include "outproc/OutProcGuiObjects.h"

// Interactive production DSP view with optional bounded engineering journal.
// Session-zero/service hosting still requires a user-session DSP broker.
// This view belongs to the already-processing component, never a second DSP.
class NativeDspViewAudit {
    EqApoControl::NativeViewChannel* channel=nullptr;
    std::wstring controlSession;
    HANDLE editorProcess=nullptr;DWORD editorProcessId=0;
    VSTPluginInstance* effect = nullptr;
    HWND window = nullptr, child = nullptr;
    ULONGLONG started = GetTickCount64(), lastSample = 0;
    bool attempted = false, detached = false;
    bool transferFinished=false;
    std::function<bool(std::uint32_t,double,int)> retainedParameterSink;
    const DWORD ownerThread=GetCurrentThreadId();
    std::ofstream journal;
    OutProcVSTConfig snapshot;
    std::wstring snapshotDirectory,snapshotPath;
    std::uint64_t observedState=0,savedState=0;
    ULONGLONG lastStateEdit=0,lastSnapshotAttempt=0;
    bool createSnapshotDirectory(){
        if(!snapshotDirectory.empty())return true;
        wchar_t temp[1024]{},id[40]{};GUID guid{};
        const auto length=GetTempPathW(1024,temp);
        if(!length || length>=1024 || FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid,id,40))return false;
        const auto prefix=std::wstring(temp)+L"EqApoNativeState-";
        auto directory=prefix+id;
        // A restarted worker keeps the generation's published path immutable.
        // Reuse only our exact GUID directory in this user's temporary folder.
        if(channel && channel->snapshotSequence.load(std::memory_order_acquire) && !channel->snapshotPath[1023]) {
            const std::wstring previous=channel->snapshotPath;
            if(previous.size()!=prefix.size()+38+11 || previous.compare(0,prefix.size(),prefix)!=0
                || previous.substr(previous.size()-11)!=L"\\state.opvs")return false;
            GUID existing{};
            if(FAILED(CLSIDFromString(previous.substr(prefix.size(),38).c_str(),&existing)))return false;
            directory=previous.substr(0,previous.size()-11);
        }
        if(directory.size()+11>=1024)return false;
        const auto user=EqApoControl::canonicalSid(EqApoControl::currentUserSid());if(user.empty())return false;
        PSECURITY_DESCRIPTOR sd=nullptr;
        const auto acl=L"D:P(A;OICI;GA;;;SY)(A;OICI;GR;;;LS)(A;OICI;GA;;;"+user+L")";
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&sd,nullptr))return false;
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),sd,FALSE};bool created=CreateDirectoryW(directory.c_str(),&attributes)!=FALSE;
        if(!created && GetLastError()==ERROR_ALREADY_EXISTS){const auto flags=GetFileAttributesW(directory.c_str());created=flags!=INVALID_FILE_ATTRIBUTES && (flags&FILE_ATTRIBUTE_DIRECTORY) && !(flags&FILE_ATTRIBUTE_REPARSE_POINT);}
        LocalFree(sd);
        if(!created)return false;snapshotDirectory=directory;snapshotPath=directory+L"\\state.opvs";
        return snapshotPath.size()<1024;
    }
    void publishState(bool force=false){
        if(!effect || !channel || snapshot.libraryPath.empty())return;
        const auto generation=effect->getStateGeneration(),now=GetTickCount64();
        if(generation!=observedState){observedState=generation;lastStateEdit=now;}
        if(!force && (generation==savedState || now-lastStateEdit<200 || now-lastSnapshotAttempt<200))return;
        if(!force && effect->hasActiveEditorGesture())return;
        lastSnapshotAttempt=now;
        if(!createSnapshotDirectory()){channel->snapshotError.store(ERROR_ACCESS_DENIED);return;}
        OutProcVSTConfig next=snapshot;effect->readFromEffect(next.chunkData,next.paramMap);
        if(!force && next.chunkData==snapshot.chunkData && next.paramMap==snapshot.paramMap){savedState=generation;return;}
        if(!OutProcWriteVSTConfig(snapshotPath,next)){channel->snapshotError.store(ERROR_WRITE_FAULT);return;}
        snapshot=std::move(next);
        if(!channel->snapshotSequence.load())wcscpy_s(channel->snapshotPath,snapshotPath.c_str());
        channel->snapshotError.store(0);channel->snapshotSequence.fetch_add(1,std::memory_order_release);
        savedState=generation;event("COMPLETE_STATE_PUBLISHED");
    }
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<NativeDspViewAudit*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<NativeDspViewAudit*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if(self && self->channel && (message==WM_MOVE || message==WM_SIZE) && !IsIconic(window)) {
            RECT rect{};if(GetWindowRect(window,&rect)) {
                self->channel->x.store(rect.left);self->channel->y.store(rect.top);
                self->channel->width.store(rect.right-rect.left);self->channel->height.store(rect.bottom-rect.top);
            }
        }
        if (self && message == WM_CLOSE) { self->close(); return 0; }
        if (self && message == WM_DPICHANGED && self->effect) {
            self->effect->setEditorScale(float(HIWORD(w))/96.0f);
            return 0;
        }
        return DefWindowProcW(window, message, w, l);
    }
    void event(const char* name) {
        if (journal) journal << "{\"event\":\"" << name << "\",\"elapsed_ms\":"
                            << GetTickCount64()-started << "}\n" << std::flush;
    }
    void resize(int width, int height) {
        if (!window || !child || width<=0 || height<=0) return;
        RECT rect{0,0,width,height}; AdjustWindowRectEx(&rect,WS_OVERLAPPEDWINDOW,FALSE,0);
        SetWindowPos(window,nullptr,0,0,rect.right-rect.left,rect.bottom-rect.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(child,nullptr,0,0,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    void relayEdit(std::uint32_t id,double value,int offset) {
        if(!channel)return;
        if(!EqApoControl::publishControl(controlSession,channel->identity,id,value,offset,channel->generation)
            && retainedParameterSink)retainedParameterSink(id,value,offset);
        channel->edits.publish(channel->generation,id,value,offset);
    }
    void refreshInheritedVisibility() {
        if(!channel || !channel->predecessor || !channel->inheritedVisibility.exchange(false))return;
        const auto user=EqApoControl::currentUserSid();
        for(const auto* space:{L"Global\\",L"Local\\"}) {
            const auto name=std::wstring(space)+L"EqApoControl_"+controlSession+L"_"+user
                +L"_"+std::to_wstring(channel->predecessor)+L"_View";
            HANDLE handle=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());if(!handle)continue;
            const auto* previous=static_cast<const EqApoControl::NativeViewChannel*>(MapViewOfFile(handle,FILE_MAP_READ,0,0,sizeof(EqApoControl::NativeViewChannel)));
            const bool valid=previous && previous->magic==EqApoControl::NativeViewChannel::magicValue
                && previous->version==EqApoControl::NativeViewChannel::protocolVersion
                && previous->generation==channel->predecessor && previous->identity==channel->identity;
            if(valid)channel->desiredVisibility.store(previous->desiredVisibility.load(std::memory_order_acquire));
            if(previous)UnmapViewOfFile(previous);CloseHandle(handle);if(valid)return;
        }
    }
public:
    void setRetainedParameterSink(std::function<bool(std::uint32_t,double,int)> sink){retainedParameterSink=std::move(sink);}
    NativeDspViewAudit(EqApoControl::NativeViewChannel* control=nullptr,const std::wstring& sessionId=L"",const OutProcVSTConfig& config={}):channel(control),controlSession(sessionId) {
        snapshot.libraryPath=config.libraryPath;snapshot.classUID=config.classUID;snapshot.vst3ClassIndex=config.vst3ClassIndex;
        DWORD session=0;
        if(channel){channel->hostPid.store(GetCurrentProcessId());channel->status.store(ProcessIdToSessionId(GetCurrentProcessId(),&session) && session!=0 ? 1 : 4);}
        wchar_t path[32768]{};
        const DWORD length=GetEnvironmentVariableW(L"EQAPO_NATIVE_DSP_VIEW_AUDIT",path,32768);
        if (!length || length>=32768) return;
        wchar_t append[4]{};const bool rebindJournal=GetEnvironmentVariableW(L"EQAPO_NATIVE_VIEW_REBIND_AUDIT",append,4)==1 && append[0]==L'1';
        journal.open(path,std::ios::out|(rebindJournal?std::ios::app:std::ios::trunc));
        if (!ProcessIdToSessionId(GetCurrentProcessId(),&session) || session==0) {
            event("NONINTERACTIVE_SESSION_UNSUPPORTED");journal.close();return;
        }
        event("AUDIT_STARTED");
    }
    ~NativeDspViewAudit() { close();if(editorProcess)CloseHandle(editorProcess);if(channel){channel->hostPid.store(0);channel->status.store(0);}if(!snapshotPath.empty())DeleteFileW(snapshotPath.c_str());if(!snapshotDirectory.empty())RemoveDirectoryW(snapshotDirectory.c_str()); }
    HWND getWindowForAudit()const noexcept{return window;}
    void recordRelayForAudit(unsigned id,double value,unsigned count){if(journal)journal<<"{\"event\":\"REBIND_GUI_RELAY\",\"id\":"<<id<<",\"value\":"<<value<<",\"count\":"<<count<<"}\n"<<std::flush;}
    bool rebind(EqApoControl::NativeViewChannel* replacement,const std::wstring& session,std::uint64_t identity){
        if(GetCurrentThreadId()!=ownerThread || !replacement || !EqApoControl::validSessionId(session)
            || replacement->magic!=EqApoControl::NativeViewChannel::magicValue || replacement->version!=EqApoControl::NativeViewChannel::protocolVersion
            || !replacement->generation || replacement->identity!=identity || (channel && channel->identity!=identity))return false;
        if(channel==replacement)return session==controlSession;
        const auto previousOwner=channel?channel->editorPid.load(std::memory_order_acquire):0;
        unsigned empty=0;
        if(previousOwner && !replacement->editorPid.compare_exchange_strong(empty,previousOwner) && empty!=previousOwner)return false;
        auto visibility=channel?channel->desiredVisibility.load(std::memory_order_acquire):0u;
        const auto pending=channel?channel->command.exchange(0):0u;
        if(pending==3)visibility=0;else if(pending==2)visibility=2;else if(pending==1)visibility=1;
        const auto nextCommand=replacement->command.load();
        if(nextCommand==3)visibility=0;
        else if(nextCommand==2)visibility=2;
        else if(nextCommand==1 && !replacement->inheritedVisibility.load())visibility=1;
        if(!visibility && window)close();
        else if(visibility==2 && window)ShowWindow(window,SW_HIDE);
        else if(visibility==1 && window && !IsWindowVisible(window))OutProcShowGuiForeground(window);
        if(channel){channel->hostPid.store(0);channel->status.store(0);channel->editorPid.store(0);}
        channel=replacement;controlSession=session;transferFinished=true;
        channel->inheritedVisibility.store(false);
        channel->desiredVisibility.store(visibility);channel->command.store(0);
        DWORD processSession=0;
        channel->hostPid.store(GetCurrentProcessId());
        channel->status.store(ProcessIdToSessionId(GetCurrentProcessId(),&processSession) && processSession!=0
            ? (window && IsWindow(window)?(IsWindowVisible(window)?2u:3u):1u):4u);
        // Existing performEdit sink captures this object and therefore follows
        // the new channel; no component, controller, IPlugView or HWND teardown.
        if(effect)effect->setParameterSink([this](std::uint32_t id,double value,int offset){
            relayEdit(id,value,offset);
        },true);
        event(window && IsWindow(window)?"CONTROL_REBOUND_VIEW_RETAINED":"CONTROL_REBOUND_NO_VIEW");return true;
    }
    void close() {
        if(channel)channel->desiredVisibility.store(0,std::memory_order_release);
        if (!effect) {if(channel)channel->status.store(1);return;}
        publishState(true);
        // This is the controller thread. The processor keeps running and
        // its component, activation and buffers are untouched by view removal.
        effect->setSizeWindowFunc(nullptr);
        effect->stopEditing();
        effect=nullptr;detached=!channel;attempted=false;
        if(channel)channel->status.store(1);
        if(window && IsWindow(window))DestroyWindow(window);
        window=nullptr;child=nullptr;event("VIEW_DETACHED_DSP_RETAINED");
    }
    void service(VSTPluginInstance* processingEffect) {
        unsigned command=0;
        if(channel) {
            if(channel->status.load()==4)return;
            command=channel->command.exchange(0);
            const auto owner=channel->editorPid.load();
            if(owner!=editorProcessId){if(editorProcess)CloseHandle(editorProcess);editorProcess=nullptr;editorProcessId=owner;if(owner)editorProcess=OpenProcess(SYNCHRONIZE,FALSE,owner);}
            if(owner && (!editorProcess || WaitForSingleObject(editorProcess,0)==WAIT_OBJECT_0)){channel->editorPid.store(0);close();return;}
            if(command==3){close();return;}
            if(command==2){channel->desiredVisibility.store(2);if(window){ShowWindow(window,SW_HIDE);channel->status.store(3);}}
            if(command==1 && window){channel->desiredVisibility.store(1);OutProcShowGuiForeground(window);channel->status.store(2);}
			if(command==4)publishState(true);
        } else if(!journal || detached)return;
        const auto elapsed=GetTickCount64()-started;
        if(channel && command==1 && !window && channel->inheritedVisibility.load()) {
            refreshInheritedVisibility();
            if(!channel->desiredVisibility.load()){channel->status.store(1);command=0;}
        }
        if(!attempted && (channel ? command==1 : elapsed>=700)) {
            attempted=true;if(channel)channel->status.store(5);effect=processingEffect;
            if(!effect){event("NO_PROCESSING_COMPONENT");detached=true;return;}
            observedState=savedState=effect->getStateGeneration();
            effect->readFromEffect(snapshot.chunkData,snapshot.paramMap);
            if(channel)effect->setParameterSink([this](std::uint32_t id,double value,int offset){
                relayEdit(id,value,offset);
            },true);
            WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandleW(nullptr);
            wc.lpszClassName=L"EqApoNativeDspViewAudit";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
            RegisterClassW(&wc);
            window=CreateWindowExW(0,wc.lpszClassName,L"Equalizer APO - VST",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                                  channel?channel->x.load():CW_USEDEFAULT,channel?channel->y.load():CW_USEDEFAULT,440,260,nullptr,nullptr,wc.hInstance,this);
            if(window)child=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,400,200,window,nullptr,wc.hInstance,nullptr);
            short width=400,height=200;
            if(!child || !effect->startEditing(child,&width,&height)) {event("VIEW_OPEN_FAILED");close();if(channel)channel->status.store(6);return;}
            if(channel && channel->desiredVisibility.load()==0 && channel->command.load()==3){close();return;}
            effect->setSizeWindowFunc([this](int w,int h){resize(w,h);});
            resize(width,height);
            const bool hidden=channel && channel->desiredVisibility.load()==2;
            if(hidden)ShowWindow(window,SW_HIDE);else OutProcShowGuiForeground(window);
            if(channel){channel->desiredVisibility.store(hidden?2u:1u);channel->status.store(hidden?3u:2u);}
            event("REAL_COMPONENT_VIEW_ATTACHED");
        }
        if(window && channel && channel->predecessor && !transferFinished
            && EqApoControl::completeNativeViewTransfer(controlSession,*channel)){
            transferFinished=true;event("GENERATION_VIEW_TRANSFER_COMPLETED");
        }
        if(effect) {
            effect->doIdle();
            publishState();
            if(journal && elapsed-lastSample>=100) {
                lastSample=elapsed;
                // Inspect actual native child text, not a mirror drawn by host.
                wchar_t text[256]{};
                EnumChildWindows(child,[](HWND view,LPARAM result)->BOOL {
                    return GetWindowTextW(view,reinterpret_cast<wchar_t*>(result),256)>0 ? FALSE : TRUE;
                },reinterpret_cast<LPARAM>(text));
                if(text[0]) {
                    char utf8[1024]{};WideCharToMultiByte(CP_UTF8,0,text,-1,utf8,1024,nullptr,nullptr);
                    std::string escaped;for(char ch:std::string(utf8)) {if(ch=='"' || ch=='\\')escaped+='\\';if(ch>=32)escaped+=ch;}
                    journal << "{\"event\":\"NATIVE_VIEW_TEXT\",\"elapsed_ms\":" << elapsed
                            << ",\"text\":\"" << escaped << "\"}\n" << std::flush;
                }
            }
            if(!channel && elapsed>=5000)close();
        }
    }
};
