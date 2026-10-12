/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "stdafx.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <sstream>

#include "helpers/LogHelper.h"
#include "OutProcBiquadFilter.h"

using namespace std;

static void outProcBiquadModuleAnchor()
{
}

OutProcBiquadFilter::OutProcBiquadFilter(BiQuad::Type type, double dbGain, double freq, double bandwidthOrQOrS, bool isBandwidthOrS, bool isCornerFreq)
	: type(type),
	  dbGain(dbGain),
	  freq(freq),
	  bandwidthOrQOrS(bandwidthOrQOrS),
	  isBandwidthOrS(isBandwidthOrS),
	  isCornerFreq(isCornerFreq),
	  sampleRate(0.0f),
	  channelCount(0),
	  maxFrameCount(0),
	  timeoutMs(1),
	  requestSeq(0),
	  sharedByteCount(0),
	  coeffA0(1.0),
	  coeffA1(0.0),
	  coeffA2(0.0),
	  coeffB1(0.0),
	  coeffB2(0.0),
	  mappingHandle(NULL),
	  requestEvent(NULL),
	  responseEvent(NULL),
	  shutdownEvent(NULL),
	  processHandle(NULL),
	  sharedHeader(nullptr),
	  launchFailureLogged(false),
	  timeoutLogged(false),
	  exitedLogged(false),
	  protocolLogged(false),
	  wasBypassed(false)
{
}

OutProcBiquadFilter::~OutProcBiquadFilter()
{
	closeHost();
}

vector<wstring> OutProcBiquadFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	this->sampleRate = sampleRate;
	this->channelCount = static_cast<unsigned>(channelNames.size());
	this->maxFrameCount = maxFrameCount;

	if (sampleRate > 0.0f && maxFrameCount > 0)
	{
		const double blockMs = (static_cast<double>(maxFrameCount) * 1000.0) / sampleRate;
		const double halfBlockMs = blockMs * 0.5;
		timeoutMs = max(1u, static_cast<unsigned>(min(5.0, halfBlockMs)));
	}
	else
	{
		timeoutMs = 1;
	}

	if (channelCount == 0 || maxFrameCount == 0 || sampleRate <= 0.0f)
	{
		LogF(L"OutProcBiquad: invalid audio format, filter will bypass");
		return channelNames;
	}

	double biquadFreq = freq;
	if (isCornerFreq && (type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF))
	{
		double s = bandwidthOrQOrS;
		if (!isBandwidthOrS)
		{
			double q = bandwidthOrQOrS;
			double a = pow(10, dbGain / 40);
			s = 1.0 / ((1.0 / (q * q) - 2.0) / (a + 1.0 / a) + 1.0);
		}
		double centerFreqFactor = pow(10.0, abs(dbGain) / 80.0 / s);
		if (type == BiQuad::LOW_SHELF)
			biquadFreq *= centerFreqFactor;
		else
			biquadFreq /= centerFreqFactor;
	}

	BiQuad masterBiquad(type, dbGain, biquadFreq, sampleRate, bandwidthOrQOrS, isBandwidthOrS);
	double coeffs[4] = {};
	masterBiquad.getCoefficients(coeffs, coeffA0);
	coeffA1 = coeffs[0];
	coeffA2 = coeffs[1];
	coeffB1 = coeffs[2];
	coeffB2 = coeffs[3];

	if (!OutProcAudioSharedMemorySize(channelCount, maxFrameCount, sharedByteCount))
	{
		LogF(L"OutProcBiquad: invalid shared memory size, filter will bypass");
		return channelNames;
	}

	audioClient.prepare(channelCount,maxFrameCount,unsigned(sampleRate));
    startHost();
	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void OutProcBiquadFilter::process(double** output, double** input, unsigned frameCount)
{
    const auto before=sharedHeader ? sharedHeader->produced.load(std::memory_order_relaxed) : 0;
    audioClient.process(output,input,frameCount,sharedHeader);
    if(sharedHeader && requestEvent && sharedHeader->produced.load(std::memory_order_relaxed)!=before)SetEvent(requestEvent);
}
#pragma AVRT_CODE_END

void OutProcBiquadFilter::closeHost()
{
	if (shutdownEvent != NULL)
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

bool OutProcBiquadFilter::startHost()
{
	closeHost();

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
    new(sharedHeader) OutProcAudioHeader();
	sharedHeader->magic = OUTPROC_AUDIO_MAGIC;
	sharedHeader->version = OUTPROC_AUDIO_VERSION;
	sharedHeader->sampleRate = static_cast<uint32_t>(sampleRate);
	sharedHeader->channelCount = channelCount;
	sharedHeader->maxFrames = maxFrameCount;
    OutProcInitializeSlots(sharedHeader);
	sharedHeader->dspType = OUTPROC_AUDIO_DSP_BIQUAD;
    sharedHeader->biquadA0=coeffA0;
    sharedHeader->biquadA1=coeffA1;
    sharedHeader->biquadA2=coeffA2;
    sharedHeader->biquadB1=coeffB1;
    sharedHeader->biquadB2=coeffB2;

	const wstring hostPath = getHostPath();
	if (hostPath.empty())
	{
		logLaunchFailure(L"host path could not be resolved");
		closeHost();
		return false;
	}

	wstringstream commandLine;
	commandLine << L"\"" << hostPath << L"\""
		<< L" --parent-pid " << GetCurrentProcessId()
        << L" --mapping " << reinterpret_cast<uintptr_t>(mappingHandle)
		<< L" --request " << reinterpret_cast<uintptr_t>(requestEvent)
		<< L" --response " << reinterpret_cast<uintptr_t>(responseEvent)
		<< L" --shutdown " << reinterpret_cast<uintptr_t>(shutdownEvent);

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
	TraceF(L"OutProcBiquad: launched EqApoOutProcHost.exe with timeout %u ms", timeoutMs);
	return true;
}

bool OutProcBiquadFilter::validateProtocol(unsigned frameCount) const
{
	return sharedHeader->magic == OUTPROC_AUDIO_MAGIC
		&& sharedHeader->version == OUTPROC_AUDIO_VERSION
		&& sharedHeader->channelCount == channelCount
		&& sharedHeader->maxFrames == maxFrameCount
		&& sharedHeader->frameCount == frameCount
		&& sharedHeader->responseSeq == requestSeq
		&& sharedHeader->status == OUTPROC_AUDIO_STATUS_OK;
}

void OutProcBiquadFilter::bypass(double** output, double** input, unsigned frameCount) const
{
	for (unsigned channel = 0; channel < channelCount; ++channel)
		for (unsigned frame = 0; frame < frameCount; ++frame)
			output[channel][frame] = input[channel][frame];

	const_cast<OutProcBiquadFilter*>(this)->wasBypassed = true;
}

void OutProcBiquadFilter::logLaunchFailure(const wstring& detail)
{
	if (!launchFailureLogged)
	{
		LogF(L"OutProcBiquad: host launch failed: %s", detail.c_str());
		launchFailureLogged = true;
	}
}

void OutProcBiquadFilter::logTimeout()
{
	if (!timeoutLogged)
	{
		LogF(L"OutProcBiquad: host timeout, bypassing audio");
		timeoutLogged = true;
	}
}

void OutProcBiquadFilter::logHostExited()
{
	if (!exitedLogged)
	{
		LogF(L"OutProcBiquad: host exited, bypassing audio");
		exitedLogged = true;
	}
}

void OutProcBiquadFilter::logProtocolMismatch()
{
	if (!protocolLogged)
	{
		LogF(L"OutProcBiquad: protocol mismatch, bypassing audio");
		protocolLogged = true;
	}
}

void OutProcBiquadFilter::logRecovered()
{
	TraceF(L"OutProcBiquad: host recovered");
	timeoutLogged = false;
	protocolLogged = false;
}

wstring OutProcBiquadFilter::getHostPath() const
{
	HMODULE moduleHandle = NULL;
	const BOOL gotModule = GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&outProcBiquadModuleAnchor),
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
