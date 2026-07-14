#pragma once

#include <string>
#include <windows.h>

void VSTDiag(const wchar_t* format, ...);
void VSTDiagText(const std::wstring& message);
void VSTWriteMiniDump(const wchar_t* tag, EXCEPTION_POINTERS* exceptionPointers);
