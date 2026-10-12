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
#include "outproc/AsyncAudioClient.h"
#include <thread>
#include <atomic>
#include <memory>
#include "outproc/OutProcControlEndpoint.h"
#include "outproc/BrokerCatalog.h"

#pragma AVRT_VTABLES_BEGIN
class OutProcVSTPluginFilter : public IFilter
{
public:
	OutProcVSTPluginFilter(std::wstring libPath, std::wstring chunkData, std::unordered_map<std::wstring, float> paramMap, std::wstring hostId = L"", bool analysisMode = false, int vst3ClassIndex = 0, std::wstring classUID = L"", std::wstring guiUserSid = L"", std::wstring brokerInstanceId = L"");
	~OutProcVSTPluginFilter() override;

	bool getInPlace() override { return true; }
    unsigned getLatencySamples() const noexcept override {return analysisMode ? 0 : audioClient.effectiveLatency();}
    bool isReadyForTransition() const noexcept override {return analysisMode || audioClient.readyForTransition() || (transitionLaunchFailed.load(std::memory_order_acquire) && audioClient.drySettled());}
    unsigned getAnalysisStatus() const noexcept override {return analysisFailed.load(std::memory_order_acquire) ? 1u : 0u;}
    double getAnalysisCpuMilliseconds() const noexcept override {
        return analysisMode && sharedHeader ? sharedHeader->analysisCpu100ns.load(std::memory_order_acquire)/10000.0 : 0;
    }

	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;
    struct Diagnostics {
        bool available=false,hostAlive=false,launchFailed=false;
        unsigned ready=0,error=0;
        unsigned hostPid=0;
        unsigned processingQuantum=0;
        std::uint64_t heartbeat=0,consumed=0,produced=0,missing=0,dropped=0,epoch=0;
        std::uint64_t parameterGeneration=0,parameterSequence=0,parameterDrops=0;
    };
    // Non-RT snapshot: does not race supervisor mapping replacement.
    Diagnostics getDiagnostics() noexcept;
    bool publishParameter(std::uint64_t generation,std::uint32_t id,double value,int offset=0) noexcept;
private:
	void closeHost();
    void processAnalysis(double** output,double** input,unsigned frames);
	bool startHost();
	bool startNamedHostSession();
	bool startInheritedHost();
    bool startBrokerHost();
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

	EqApoAsync::AudioClient audioClient;
    std::atomic<bool> transitionLaunchFailed{true};
    std::thread supervisor;
    std::atomic<bool> stopping{false};
    std::atomic<unsigned> realtimeGate{0};
    // Control readers pin mapping lifetime without excluding the audio
    // owner. Only session replacement closes this bounded reader admission.
    std::atomic<bool> mappingChanging{false};
    std::atomic<unsigned> sessionReaders{0};
    std::atomic_flag parameterWriter=ATOMIC_FLAG_INIT;
    std::atomic<bool> analysisFailed{false};
    unsigned restartAttempts=0;
    std::uint64_t nativeRecoverySequence=0;
    std::uint64_t sessionEpoch=0;
    void supervise();
    std::wstring guiUserSid;
    std::unique_ptr<EqApoControl::ControlEndpointOwner> controlEndpoint;
    std::unique_ptr<EqApoBroker::Owner> brokerSession;
    std::unique_ptr<EqApoBroker::Catalog> brokerCatalog;
    bool brokerMode=false;
    EqApoControl::ParameterEvent pendingControl;
    bool hasPendingControl=false;
    OutProcVSTConfig vstConfig;
	std::wstring configFilePath;
	std::wstring hostId;
    std::wstring brokerInstanceId;
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
