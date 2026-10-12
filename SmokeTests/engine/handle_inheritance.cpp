#include "outproc/InheritedProcess.h"
#include <iostream>

int wmain(int argc,wchar_t** argv) {
    if(argc==3){const bool accepted=SetEvent(reinterpret_cast<HANDLE>(std::stoull(argv[1])))!=FALSE;
        SetEvent(reinterpret_cast<HANDLE>(std::stoull(argv[2])));return accepted?0:2;}
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    HANDLE allowed=CreateEventW(&security,TRUE,FALSE,nullptr),excluded=CreateEventW(&security,TRUE,FALSE,nullptr);
    if(!allowed || !excluded)return 3;
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
    auto trial=[&](bool restricted){
        ResetEvent(allowed);ResetEvent(excluded);PROCESS_INFORMATION child{};
        std::wstring command=L"\""+std::wstring(path)+L"\" "+std::to_wstring(reinterpret_cast<uintptr_t>(allowed))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(excluded));
        bool started=false;
        if(restricted)started=OutProcCreateRestrictedProcess(path,command,L"",{allowed},child);
        else {STARTUPINFOW info{};info.cb=sizeof(info);started=CreateProcessW(path,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&info,&child)!=FALSE;}
        if(!started)return false;
        const auto waited=WaitForSingleObject(child.hProcess,10000);DWORD code=99;GetExitCodeProcess(child.hProcess,&code);
        if(waited!=WAIT_OBJECT_0){TerminateProcess(child.hProcess,99);WaitForSingleObject(child.hProcess,1000);}
        CloseHandle(child.hThread);CloseHandle(child.hProcess);
        return waited==WAIT_OBJECT_0 && code==0 && WaitForSingleObject(allowed,0)==WAIT_OBJECT_0
            && (WaitForSingleObject(excluded,0)==WAIT_OBJECT_0)==!restricted;
    };
    const bool baseline=trial(false),fixed=trial(true);CloseHandle(allowed);CloseHandle(excluded);
    std::cout<<"unrestricted_reproduces_leak="<<baseline<<" restricted_isolates="<<fixed<<'\n';
    return baseline && fixed?0:1;
}
