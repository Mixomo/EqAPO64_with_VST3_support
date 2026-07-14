/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "IFilter.h"
#include "outproc/OutProcAudioProtocol.h"
#include "outproc/OutProcVSTConfig.h"

#pragma AVRT_VTABLES_BEGIN
class OutProcVSTPluginFilter : public IFilter
{
public:
	OutProcVSTPluginFilter(std::wstring libPath, std::wstring chunkData, std::unordered_map<std::wstring, float> paramMap, std::wstring hostId = L"", bool analysisMode = false, int vst3ClassIndex = 0);
	~OutProcVSTPluginFilter() override;

	bool getInPlace() override { return true; }

	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;
private:
	void closeHost();
	bool startHost();
	bool startNamedHostSession();
	bool startInheritedHost();
	bool writeConfigFile();
	bool validateProtocol(unsigned frameCount) const;
	std::wstring makeObjectName(const wchar_t* suffix) const;
	SECURITY_ATTRIBUTES* createOpenSecurityAttributes(PSECURITY_DESCRIPTOR& securityDescriptor) const;
	void bypass(double** output, double** input, unsigned frameCount) const;
	void logLaunchFailure(const std::wstring& detail);
	void logTimeout();
	void logHostExited();
	void logProtocolMismatch();
	void disableAfterFailure(const std::wstring& reason);
	void logRecovered();
	std::wstring getHostPath() const;

	OutProcVSTConfig vstConfig;
	std::wstring configFilePath;
	std::wstring hostId;
	float sampleRate;
	unsigned channelCount;
	unsigned maxFrameCount;
	unsigned timeoutMs;
	std::uint64_t requestSeq;
	std::size_t sharedByteCount;

	HANDLE mappingHandle;
	HANDLE requestEvent;
	HANDLE responseEvent;
	HANDLE shutdownEvent;
	HANDLE processHandle;
	OutProcAudioHeader* sharedHeader;

	bool namedHostMode;
	bool analysisMode;
	bool launchFailureLogged;
	bool timeoutLogged;
	bool exitedLogged;
	bool protocolLogged;
	bool wasBypassed;
	bool disabled;
	unsigned consecutiveTimeouts;
};
#pragma AVRT_VTABLES_END
