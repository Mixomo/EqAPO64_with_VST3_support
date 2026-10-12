#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
inline bool EqApoFeatureEnabled(const wchar_t* name,bool fallback=true) noexcept {
    DWORD value=fallback ? 1u : 0u,size=sizeof(value);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\EqualizerAPO\\Features",name,RRF_RT_REG_DWORD|RRF_SUBKEY_WOW6464KEY,nullptr,&value,&size)!=ERROR_SUCCESS) return fallback;
    return value!=0;
}
