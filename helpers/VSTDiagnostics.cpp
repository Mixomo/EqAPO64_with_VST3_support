#include "stdafx.h"
#include "VSTDiagnostics.h"

#include <cstdarg>
#include <fstream>
#include <dbghelp.h>

using namespace std;

static wstring vstDiagTempDirectory()
{
	wchar_t tempPath[MAX_PATH] = {};
	const DWORD length = GetTempPathW(MAX_PATH, tempPath);
	if (length == 0 || length >= MAX_PATH)
		return L"";
	return wstring(tempPath);
}

static wstring combinePath(const wstring& directory, const wchar_t* fileName)
{
	if (directory.empty())
		return L"";
	const wchar_t last = directory[directory.size() - 1];
	if (last == L'\\' || last == L'/')
		return directory + fileName;
	return directory + L"\\" + fileName;
}

static wstring vstDiagLogPath()
{
	const wstring tempDirectory = vstDiagTempDirectory();
	return combinePath(tempDirectory, L"EqApoVST-diagnostics.log");
}

void VSTDiagText(const wstring& message)
{
	const wstring path = vstDiagLogPath();
	if (path.empty())
		return;

	SYSTEMTIME st;
	GetLocalTime(&st);

	wofstream stream(path, ios::app);
	if (!stream)
		return;

	stream << L"[" << st.wYear << L"-";
	stream.width(2); stream.fill(L'0'); stream << st.wMonth << L"-";
	stream.width(2); stream << st.wDay << L" ";
	stream.width(2); stream << st.wHour << L":";
	stream.width(2); stream << st.wMinute << L":";
	stream.width(2); stream << st.wSecond << L".";
	stream.width(3); stream << st.wMilliseconds;
	stream.fill(L' ');
	stream << L" pid=" << GetCurrentProcessId() << L" tid=" << GetCurrentThreadId() << L"] ";
	stream << message << endl;
}

void VSTDiag(const wchar_t* format, ...)
{
	wchar_t buffer[4096] = {};
	va_list args;
	va_start(args, format);
	_vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
	va_end(args);
	VSTDiagText(buffer);
}

void VSTWriteMiniDump(const wchar_t* tag, EXCEPTION_POINTERS* exceptionPointers)
{
	const wstring tempDirectory = vstDiagTempDirectory();
	if (tempDirectory.empty())
		return;

	SYSTEMTIME st;
	GetLocalTime(&st);

	wchar_t fileName[256] = {};
	_snwprintf_s(fileName, _countof(fileName), _TRUNCATE,
		L"EqApoVST-%s-pid%lu-%04u%02u%02u-%02u%02u%02u-%03u.dmp",
		(tag && *tag) ? tag : L"exception",
		GetCurrentProcessId(),
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	const wstring dumpPath = combinePath(tempDirectory, fileName);
	HANDLE file = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		VSTDiag(L"minidump create failed path=%s gle=%lu", dumpPath.c_str(), GetLastError());
		return;
	}

	MINIDUMP_EXCEPTION_INFORMATION exceptionInfo = {};
	exceptionInfo.ThreadId = GetCurrentThreadId();
	exceptionInfo.ExceptionPointers = exceptionPointers;
	exceptionInfo.ClientPointers = FALSE;

	MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
		MiniDumpWithIndirectlyReferencedMemory |
		MiniDumpWithDataSegs |
		MiniDumpWithThreadInfo |
		MiniDumpWithUnloadedModules);

	const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, dumpType,
		exceptionPointers ? &exceptionInfo : NULL, NULL, NULL);
	const DWORD gle = GetLastError();
	CloseHandle(file);

	if (ok)
		VSTDiag(L"minidump written path=%s", dumpPath.c_str());
	else
		VSTDiag(L"minidump write failed path=%s gle=%lu", dumpPath.c_str(), gle);
}
