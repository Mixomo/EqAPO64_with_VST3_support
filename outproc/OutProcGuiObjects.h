#pragma once
#include <windows.h>
#include <string>

// GUI/control thread only. Raise on an explicit show without making the panel topmost.
inline void OutProcShowGuiForeground(HWND window)
{
    if (!window) return;
    ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(window);
    SetActiveWindow(window);
}

// Editor and its GUI child share an interactive Windows session. Global
// mappings require SeCreateGlobalPrivilege, which a normal Editor lacks.
// Audio IPC has its own inherited handles and must not use this namespace.
inline std::wstring OutProcGuiObjectName(const std::wstring& sessionId, const wchar_t* suffix)
{
    std::wstring safeId = sessionId;
    for (wchar_t& ch : safeId) {
        const bool ok = (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'z')
            || (ch >= L'A' && ch <= L'Z') || ch == L'-' || ch == L'_';
        if (!ok) ch = L'_';
    }
    return L"Local\\EqApoOutProcVST_" + safeId + L"_" + suffix;
}
