#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "outproc/OutProcAudioProtocol.h"
#include "outproc/OutProcGuiObjects.h"
#include <iostream>

int main()
{
    const auto name = OutProcGuiObjectName(L"regression/" + std::to_wstring(GetCurrentProcessId()), L"GuiInfo");
    HANDLE writer = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(OutProcGuiInfo), name.c_str());
    if (!writer) return 1;
    auto* writable = static_cast<OutProcGuiInfo*>(MapViewOfFile(writer, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(OutProcGuiInfo)));
    if (!writable) { CloseHandle(writer); return 2; }
    new(writable) OutProcGuiInfo();
    writable->magic = OUTPROC_GUI_INFO_MAGIC;
    writable->version = OUTPROC_GUI_INFO_VERSION;
    writable->stateGeneration.store(42, std::memory_order_release);
    HANDLE reader = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    auto* readable = reader ? static_cast<const OutProcGuiInfo*>(MapViewOfFile(reader, FILE_MAP_READ, 0, 0, sizeof(OutProcGuiInfo))) : nullptr;
    const bool valid = readable && readable->magic == OUTPROC_GUI_INFO_MAGIC && readable->stateGeneration.load(std::memory_order_acquire) == 42;
    if (readable) UnmapViewOfFile(readable);
    if (reader) CloseHandle(reader);
    UnmapViewOfFile(writable); CloseHandle(writer);
    std::cout << (valid ? "PASS" : "FAIL") << " interactive GUI mapping without global privilege\n";
    return valid ? 0 : 3;
}
