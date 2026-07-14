/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "stdafx.h"

#include <algorithm>
#include <cwchar>
#include <sstream>
#include <sddl.h>

#include "helpers/LogHelper.h"
#include "OutProcVSTPluginFilter.h"

using namespace std;

static void outProcVSTModuleAnchor()
{
}

OutProcVSTPluginFilter::OutProcVSTPluginFilter(wstring libPath, wstring chunkData, unordered_map<wstring, float> paramMap, wstring hostId, bool analysisMode, int vst3ClassIndex)
	: sampleRate(0.0f),
	  hostId(hostId),
	  channelCount(0),
	  maxFrameCount(0),
	  timeoutMs(1),
	  requestSeq(0),
	  sharedByteCount(0),
	  mappingHandle(NULL),
	  requestEvent(NULL),
	  responseEvent(NULL),
	  shutdownEvent(NULL),
	  processHandle(NULL),
	  sharedHeader(nullptr),
	  namedHostMode(!hostId.empty() && !analysisMode),
	  analysisMode(analysisMode),
	  launchFailureLogged(false),
	  timeoutLogged(false),
	  exitedLogged(false),
	  protocolLogged(false),
	  wasBypassed(false),
	  disabled(false),
	  consecutiveTimeouts(0)
{
	vstConfig.libraryPath = libPath;
	vstConfig.vst3ClassIndex = vst3ClassIndex;
	vstConfig.chunkData = chunkData;
	vstConfig.paramMap = paramMap;
}

OutProcVSTPluginFilter::~OutProcVSTPluginFilter()
{
	closeHost();
	if (!configFilePath.empty())
		DeleteFileW(configFilePath.c_str());
}

vector<wstring> OutProcVSTPluginFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	this->sampleRate = sampleRate;
	this->channelCount = static_cast<unsigned>(channelNames.size());
	this->maxFrameCount = maxFrameCount;

	if (sampleRate > 0.0f && maxFrameCount > 0)
	{
		const double blockMs = (static_cast<double>(maxFrameCount) * 1000.0) / sampleRate;
		const double halfBlockMs = blockMs * 0.5;
		if (analysisMode)
			timeoutMs = max(100u, static_cast<unsigned>(min(2000.0, max(100.0, blockMs * 2.0))));
		else
			timeoutMs = max(1u, static_cast<unsigned>(min(5.0, halfBlockMs)));
	}
	else
		timeoutMs = 1;

	if (channelCount == 0 || maxFrameCount == 0 || sampleRate <= 0.0f)
	{
		LogF(L"OutProcVSTPlugin: invalid audio format, filter will bypass");
		return channelNames;
	}

	if (!OutProcAudioSharedMemorySize(channelCount, maxFrameCount, sharedByteCount))
	{
		LogF(L"OutProcVSTPlugin: invalid shared memory size, filter will bypass");
		return channelNames;
	}

	startHost();
	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void OutProcVSTPluginFilter::process(double** output, double** input, unsigned frameCount)
{
	if (disabled || frameCount == 0 || frameCount > maxFrameCount || sharedHeader == nullptr || (!namedHostMode && processHandle == NULL))
	{
		bypass(output, input, frameCount);
		return;
	}

	if (!namedHostMode)
	{
		const DWORD processState = WaitForSingleObject(processHandle, 0);
		if (processState == WAIT_OBJECT_0 || processState == WAIT_FAILED)
		{
			logHostExited();
			disabled = true;
			bypass(output, input, frameCount);
			return;
		}
	}

	double* sharedInput = OutProcAudioInput(sharedHeader);
	for (unsigned channel = 0; channel < channelCount; ++channel)
	{
		double* channelInput = sharedInput + static_cast<size_t>(channel) * maxFrameCount;
		for (unsigned frame = 0; frame < frameCount; ++frame)
			channelInput[frame] = input[channel][frame];
	}

	sharedHeader->frameCount = frameCount;
	sharedHeader->dspType = OUTPROC_AUDIO_DSP_VST;
	sharedHeader->status = OUTPROC_AUDIO_STATUS_OK;
	sharedHeader->requestSeq = ++requestSeq;

	ResetEvent(responseEvent);
	if (!SetEvent(requestEvent))
	{
		logHostExited();
		bypass(output, input, frameCount);
		return;
	}

	const DWORD waitResult = WaitForSingleObject(responseEvent, timeoutMs);
	if (waitResult != WAIT_OBJECT_0)
	{
		logTimeout();
		consecutiveTimeouts++;
		if (!namedHostMode && !analysisMode && consecutiveTimeouts >= 1)
			disableAfterFailure(L"timeout");
		bypass(output, input, frameCount);
		return;
	}

	if (!validateProtocol(frameCount))
	{
		logProtocolMismatch();
		if (!namedHostMode && !analysisMode)
			disableAfterFailure(L"protocol mismatch");
		bypass(output, input, frameCount);
		return;
	}

	double* sharedOutput = OutProcAudioOutput(sharedHeader);
	for (unsigned channel = 0; channel < channelCount; ++channel)
	{
		double* channelOutput = sharedOutput + static_cast<size_t>(channel) * maxFrameCount;
		for (unsigned frame = 0; frame < frameCount; ++frame)
			output[channel][frame] = channelOutput[frame];
	}

	if (wasBypassed)
		logRecovered();
	wasBypassed = false;
	consecutiveTimeouts = 0;
}
#pragma AVRT_CODE_END

void OutProcVSTPluginFilter::closeHost()
{
	if (!namedHostMode && shutdownEvent != NULL)
		SetEvent(shutdownEvent);

	if (processHandle != NULL)
	{
		if (WaitForSingleObject(processHandle, 1000) == WAIT_TIMEOUT)
			TerminateProcess(processHandle, 0);
		CloseHandle(processHandle);
		processHandle = NULL;
	}

	if (sharedHeader != nullptr)
	{
		UnmapViewOfFile(sharedHeader);
		sharedHeader = nullptr;
	}

	if (mappingHandle != NULL)
		CloseHandle(mappingHandle);
	if (requestEvent != NULL)
		CloseHandle(requestEvent);
	if (responseEvent != NULL)
		CloseHandle(responseEvent);
	if (shutdownEvent != NULL)
		CloseHandle(shutdownEvent);

	mappingHandle = NULL;
	requestEvent = NULL;
	responseEvent = NULL;
	shutdownEvent = NULL;
}

bool OutProcVSTPluginFilter::startHost()
{
	closeHost();

	if (namedHostMode)
		return startNamedHostSession();

	return startInheritedHost();
}

bool OutProcVSTPluginFilter::startNamedHostSession()
{
	PSECURITY_DESCRIPTOR securityDescriptor = NULL;
	SECURITY_ATTRIBUTES* securityAttributes = createOpenSecurityAttributes(securityDescriptor);

	const DWORD mappingSizeHigh = static_cast<DWORD>(sharedByteCount >> 32);
	const DWORD mappingSizeLow = static_cast<DWORD>(sharedByteCount & 0xffffffff);
	mappingHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, securityAttributes, PAGE_READWRITE, mappingSizeHigh, mappingSizeLow, makeObjectName(L"Map").c_str());
	requestEvent = CreateEventW(securityAttributes, FALSE, FALSE, makeObjectName(L"Request").c_str());
	responseEvent = CreateEventW(securityAttributes, FALSE, FALSE, makeObjectName(L"Response").c_str());
	shutdownEvent = CreateEventW(securityAttributes, TRUE, FALSE, makeObjectName(L"Shutdown").c_str());

	if (securityDescriptor != NULL)
		LocalFree(securityDescriptor);

	if (mappingHandle == NULL || requestEvent == NULL || responseEvent == NULL || shutdownEvent == NULL)
	{
		logLaunchFailure(L"named Win32 handle creation failed");
		closeHost();
		return false;
	}
	ResetEvent(shutdownEvent);
	ResetEvent(requestEvent);
	ResetEvent(responseEvent);

	sharedHeader = static_cast<OutProcAudioHeader*>(MapViewOfFile(mappingHandle, FILE_MAP_ALL_ACCESS, 0, 0, sharedByteCount));
	if (sharedHeader == nullptr)
	{
		logLaunchFailure(L"named shared memory map failed");
		closeHost();
		return false;
	}

	ZeroMemory(sharedHeader, sharedByteCount);
	sharedHeader->magic = OUTPROC_AUDIO_MAGIC;
	sharedHeader->version = OUTPROC_AUDIO_VERSION;
	sharedHeader->sampleRate = static_cast<uint32_t>(sampleRate);
	sharedHeader->channelCount = channelCount;
	sharedHeader->maxFrames = maxFrameCount;
	sharedHeader->dspType = OUTPROC_AUDIO_DSP_VST;

	TraceF(L"OutProcVSTPlugin: waiting for visible host session %s with timeout %u ms", hostId.c_str(), timeoutMs);
	disabled = false;
	consecutiveTimeouts = 0;
	return true;
}

bool OutProcVSTPluginFilter::startInheritedHost()
{
	if (!writeConfigFile())
	{
		logLaunchFailure(L"could not write VST config file");
		return false;
	}

	SECURITY_ATTRIBUTES inheritableAttributes = {};
	inheritableAttributes.nLength = sizeof(inheritableAttributes);
	inheritableAttributes.bInheritHandle = TRUE;

	const DWORD mappingSizeHigh = static_cast<DWORD>(sharedByteCount >> 32);
	const DWORD mappingSizeLow = static_cast<DWORD>(sharedByteCount & 0xffffffff);
	mappingHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, &inheritableAttributes, PAGE_READWRITE, mappingSizeHigh, mappingSizeLow, NULL);
	requestEvent = CreateEventW(&inheritableAttributes, FALSE, FALSE, NULL);
	responseEvent = CreateEventW(&inheritableAttributes, FALSE, FALSE, NULL);
	shutdownEvent = CreateEventW(&inheritableAttributes, TRUE, FALSE, NULL);

	if (mappingHandle == NULL || requestEvent == NULL || responseEvent == NULL || shutdownEvent == NULL)
	{
		logLaunchFailure(L"Win32 handle creation failed");
		closeHost();
		return false;
	}

	sharedHeader = static_cast<OutProcAudioHeader*>(MapViewOfFile(mappingHandle, FILE_MAP_ALL_ACCESS, 0, 0, sharedByteCount));
	if (sharedHeader == nullptr)
	{
		logLaunchFailure(L"shared memory map failed");
		closeHost();
		return false;
	}

	ZeroMemory(sharedHeader, sharedByteCount);
	sharedHeader->magic = OUTPROC_AUDIO_MAGIC;
	sharedHeader->version = OUTPROC_AUDIO_VERSION;
	sharedHeader->sampleRate = static_cast<uint32_t>(sampleRate);
	sharedHeader->channelCount = channelCount;
	sharedHeader->maxFrames = maxFrameCount;
	sharedHeader->dspType = OUTPROC_AUDIO_DSP_VST;

	const wstring hostPath = getHostPath();
	if (hostPath.empty())
	{
		logLaunchFailure(L"host path could not be resolved");
		closeHost();
		return false;
	}

	wstringstream commandLine;
	commandLine << L"\"" << hostPath << L"\""
		<< L" --mapping " << reinterpret_cast<uintptr_t>(mappingHandle)
		<< L" --request " << reinterpret_cast<uintptr_t>(requestEvent)
		<< L" --response " << reinterpret_cast<uintptr_t>(responseEvent)
		<< L" --shutdown " << reinterpret_cast<uintptr_t>(shutdownEvent)
		<< L" --vst-config \"" << configFilePath << L"\"";

	wstring commandLineStorage = commandLine.str();
	wstring workingDirectory = hostPath.substr(0, hostPath.find_last_of(L"\\/"));

	STARTUPINFOW startupInfo = {};
	startupInfo.cb = sizeof(startupInfo);
	PROCESS_INFORMATION processInformation = {};

	if (!CreateProcessW(hostPath.c_str(), &commandLineStorage[0], NULL, NULL, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, NULL,
		workingDirectory.empty() ? NULL : workingDirectory.c_str(), &startupInfo, &processInformation))
	{
		logLaunchFailure(hostPath);
		closeHost();
		return false;
	}

	CloseHandle(processInformation.hThread);
	processHandle = processInformation.hProcess;
	disabled = false;
	consecutiveTimeouts = 0;
	TraceF(L"OutProcVSTPlugin: launched EqApoOutProcHost.exe for %s with timeout %u ms%s", vstConfig.libraryPath.c_str(), timeoutMs, analysisMode ? L" (analysis)" : L"");
	return true;
}

wstring OutProcVSTPluginFilter::makeObjectName(const wchar_t* suffix) const
{
	wstring safeId = hostId;
	for (wchar_t& ch : safeId)
	{
		const bool ok = (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || ch == L'-' || ch == L'_';
		if (!ok)
			ch = L'_';
	}
	return L"Global\\EqApoOutProcVST_" + safeId + L"_" + suffix;
}

SECURITY_ATTRIBUTES* OutProcVSTPluginFilter::createOpenSecurityAttributes(PSECURITY_DESCRIPTOR& securityDescriptor) const
{
	static SECURITY_ATTRIBUTES attributes;
	securityDescriptor = NULL;
	ZeroMemory(&attributes, sizeof(attributes));
	attributes.nLength = sizeof(attributes);
	attributes.bInheritHandle = FALSE;
	if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)", SDDL_REVISION_1, &securityDescriptor, NULL))
		attributes.lpSecurityDescriptor = securityDescriptor;
	return &attributes;
}

bool OutProcVSTPluginFilter::writeConfigFile()
{
	if (!configFilePath.empty())
		DeleteFileW(configFilePath.c_str());

	wchar_t tempPath[MAX_PATH] = {};
	wchar_t tempFile[MAX_PATH] = {};
	if (GetTempPathW(MAX_PATH, tempPath) == 0)
		return false;
	if (GetTempFileNameW(tempPath, L"EAV", 0, tempFile) == 0)
		return false;

	configFilePath = tempFile;
	return OutProcWriteVSTConfig(configFilePath, vstConfig);
}

bool OutProcVSTPluginFilter::validateProtocol(unsigned frameCount) const
{
	return sharedHeader->magic == OUTPROC_AUDIO_MAGIC
		&& sharedHeader->version == OUTPROC_AUDIO_VERSION
		&& sharedHeader->channelCount == channelCount
		&& sharedHeader->maxFrames == maxFrameCount
		&& sharedHeader->frameCount == frameCount
		&& sharedHeader->responseSeq == requestSeq
		&& sharedHeader->status == OUTPROC_AUDIO_STATUS_OK;
}

void OutProcVSTPluginFilter::bypass(double** output, double** input, unsigned frameCount) const
{
	for (unsigned channel = 0; channel < channelCount; ++channel)
		for (unsigned frame = 0; frame < frameCount; ++frame)
			output[channel][frame] = input[channel][frame];

	const_cast<OutProcVSTPluginFilter*>(this)->wasBypassed = true;
}

void OutProcVSTPluginFilter::logLaunchFailure(const wstring& detail)
{
	if (!launchFailureLogged)
	{
		LogF(L"OutProcVSTPlugin: host launch failed: %s", detail.c_str());
		launchFailureLogged = true;
	}
}

void OutProcVSTPluginFilter::logTimeout()
{
	if (!timeoutLogged)
	{
		LogF(L"OutProcVSTPlugin: host timeout, bypassing audio");
		timeoutLogged = true;
	}
}

void OutProcVSTPluginFilter::logHostExited()
{
	if (!exitedLogged)
	{
		LogF(L"OutProcVSTPlugin: host exited, bypassing audio");
		exitedLogged = true;
	}
}

void OutProcVSTPluginFilter::logProtocolMismatch()
{
	if (!protocolLogged)
	{
		LogF(L"OutProcVSTPlugin: protocol mismatch, bypassing audio");
		protocolLogged = true;
	}
}

void OutProcVSTPluginFilter::disableAfterFailure(const wstring& reason)
{
	if (!disabled)
		LogF(L"OutProcVSTPlugin: disabling out-of-process host after %s; audio will bypass until the configuration is reloaded", reason.c_str());

	disabled = true;
	if (processHandle != NULL)
	{
		TerminateProcess(processHandle, 1);
		CloseHandle(processHandle);
		processHandle = NULL;
	}
}

void OutProcVSTPluginFilter::logRecovered()
{
	TraceF(L"OutProcVSTPlugin: host recovered");
	timeoutLogged = false;
	protocolLogged = false;
}

wstring OutProcVSTPluginFilter::getHostPath() const
{
	HMODULE moduleHandle = NULL;
	const BOOL gotModule = GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&outProcVSTModuleAnchor),
		&moduleHandle);

	if (!gotModule || moduleHandle == NULL)
		return L"";

	wchar_t modulePath[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(moduleHandle, modulePath, MAX_PATH);
	if (length == 0 || length == MAX_PATH)
		return L"";

	wstring path(modulePath, length);
	const size_t slash = path.find_last_of(L"\\/");
	if (slash == wstring::npos)
		return L"EqApoOutProcHost.exe";

	return path.substr(0, slash + 1) + L"EqApoOutProcHost.exe";
}
