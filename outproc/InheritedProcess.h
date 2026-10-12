#pragma once
#include <windows.h>
#include <string>
#include <vector>

// Control thread only. Restrict inheritance to this session's exact handles;
// concurrent host launches must not retain another graph's kernel objects.
inline bool OutProcCreateRestrictedProcess(const std::wstring& executable,std::wstring& command,
    const std::wstring& directory,const std::vector<HANDLE>& handles,PROCESS_INFORMATION& process) {
    SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);
    if(!size || handles.empty())return false;
    std::vector<unsigned char> storage(size);
    auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(attributes,1,0,&size))return false;
    struct Cleanup {LPPROC_THREAD_ATTRIBUTE_LIST value;~Cleanup(){DeleteProcThreadAttributeList(value);}} cleanup{attributes};
    if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        const_cast<HANDLE*>(handles.data()),handles.size()*sizeof(HANDLE),nullptr,nullptr))return false;
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
    return CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
        CREATE_NO_WINDOW|NORMAL_PRIORITY_CLASS|EXTENDED_STARTUPINFO_PRESENT,nullptr,
        directory.empty()?nullptr:directory.c_str(),&startup.StartupInfo,&process)!=FALSE;
}
