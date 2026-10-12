/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

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
#include <vector>

#include "BiQuad.h"
#include "IFilter.h"
#include "outproc/OutProcAudioProtocol.h"
#include "outproc/AsyncAudioClient.h"

#pragma AVRT_VTABLES_BEGIN
class OutProcBiquadFilter : public IFilter
{
public:
	OutProcBiquadFilter(BiQuad::Type type, double dbGain, double freq, double bandwidthOrQOrS, bool isBandwidthOrS, bool isCornerFreq);
	~OutProcBiquadFilter() override;

	bool getInPlace() override { return true; }
    unsigned getLatencySamples() const noexcept override {return audioClient.effectiveLatency();}

	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
    EqApoAsync::AudioClient audioClient;
	void closeHost();
	bool startHost();
	bool validateProtocol(unsigned frameCount) const;
	void bypass(double** output, double** input, unsigned frameCount) const;
	void logLaunchFailure(const std::wstring& detail);
	void logTimeout();
	void logHostExited();
	void logProtocolMismatch();
	void logRecovered();
	std::wstring getHostPath() const;

	BiQuad::Type type;
	double dbGain;
	double freq;
	double bandwidthOrQOrS;
	bool isBandwidthOrS;
	bool isCornerFreq;
	float sampleRate;
	unsigned channelCount;
	unsigned maxFrameCount;
	unsigned timeoutMs;
	std::uint64_t requestSeq;
	std::size_t sharedByteCount;
	double coeffA0;
	double coeffA1;
	double coeffA2;
	double coeffB1;
	double coeffB2;

	HANDLE mappingHandle;
	HANDLE requestEvent;
	HANDLE responseEvent;
	HANDLE shutdownEvent;
	HANDLE processHandle;
	OutProcAudioHeader* sharedHeader;

	bool launchFailureLogged;
	bool timeoutLogged;
	bool exitedLogged;
	bool protocolLogged;
	bool wasBypassed;
};
#pragma AVRT_VTABLES_END
