#pragma once
#include <windows.h>
#include <wtsapi32.h>
#include <sddl.h>
#include <string>
#include <vector>
#include "outproc/OutProcControlEndpoint.h"

// Control-thread discovery only. A persisted GUI identity always wins; old
// configs without it use the ordinary current user or Windows console user.
// No impersonation, credential transfer, or registry/license copying.
inline std::wstring resolveVSTUserSid(const std::wstring& preferred) {
    auto ordinary=[](const std::wstring& sid){return sid.rfind(L"S-1-5-21-",0)==0;};
    auto sid=EqApoControl::canonicalSid(preferred);
    if(ordinary(sid))return sid;
    sid=EqApoControl::currentUserSid();
    if(ordinary(sid))return sid;
    const DWORD session=WTSGetActiveConsoleSessionId();
    if(session==0xffffffff)return {};
    HMODULE module=LoadLibraryExW(L"wtsapi32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module)return {};
    using Query=BOOL(WINAPI*)(HANDLE,DWORD,WTS_INFO_CLASS,LPWSTR*,DWORD*);
    using Free=void(WINAPI*)(PVOID);
    auto query=reinterpret_cast<Query>(GetProcAddress(module,"WTSQuerySessionInformationW"));
    auto release=reinterpret_cast<Free>(GetProcAddress(module,"WTSFreeMemory"));
    std::wstring account;
    if(query && release){
        LPWSTR user=nullptr,domain=nullptr;DWORD bytes=0;
        if(query(nullptr,session,WTSUserName,&user,&bytes) && user && *user){
            if(query(nullptr,session,WTSDomainName,&domain,&bytes) && domain && *domain)account=std::wstring(domain)+L"\\";
            account+=user;
        }
        if(user)release(user);if(domain)release(domain);
    }
    FreeLibrary(module);
    if(account.empty())return {};
    DWORD sidBytes=0,domainChars=0;SID_NAME_USE type{};
    LookupAccountNameW(nullptr,account.c_str(),nullptr,&sidBytes,nullptr,&domainChars,&type);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || !sidBytes)return {};
    std::vector<BYTE> buffer(sidBytes);std::vector<wchar_t> domain(domainChars?domainChars:1);
    if(!LookupAccountNameW(nullptr,account.c_str(),buffer.data(),&sidBytes,domain.data(),&domainChars,&type) || type!=SidTypeUser)return {};
    LPWSTR text=nullptr;
    if(!ConvertSidToStringSidW(buffer.data(),&text))return {};
    sid=text;LocalFree(text);
    return ordinary(sid)?sid:std::wstring{};
}
