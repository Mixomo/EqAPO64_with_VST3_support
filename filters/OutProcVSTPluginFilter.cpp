/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "stdafx.h"
#include "outproc/InheritedProcess.h"
#include "outproc/VSTUserContext.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <sstream>
#include <sddl.h>

#include "helpers/LogHelper.h"
#include "OutProcVSTPluginFilter.h"

using namespace std;

static void outProcVSTModuleAnchor()
{
}

OutProcVSTPluginFilter::OutProcVSTPluginFilter(wstring libPath, wstring chunkData, unordered_map<wstring, float> paramMap, wstring hostId, bool analysisMode, int vst3ClassIndex, std::wstring classUID,std::wstring guiUserSid,std::wstring brokerInstanceId)
	: sampleRate(0.0f),
	  hostId(hostId),
      brokerInstanceId(std::move(brokerInstanceId)),
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
	  namedHostMode(false), // Each graph owns a private DSP session; GUI state is a control snapshot.
	  analysisMode(analysisMode),
	  launchFailureLogged(false),
	  timeoutLogged(false),
	  exitedLogged(false),
	  protocolLogged(false),
	  wasBypassed(false),
	  disabled(false),
	  consecutiveTimeouts(0)
{
    this->guiUserSid=analysisMode?std::move(guiUserSid):resolveVSTUserSid(guiUserSid);
	vstConfig.libraryPath = libPath;
	vstConfig.vst3ClassIndex = vst3ClassIndex;
    vstConfig.classUID=classUID;
	vstConfig.chunkData = chunkData;
	vstConfig.paramMap = paramMap;
}

OutProcVSTPluginFilter::~OutProcVSTPluginFilter()
{
	stopping.store(true);
    if(supervisor.joinable()) supervisor.join();
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

	if (channelCount == 0 || maxFrameCount == 0 || !std::isfinite(sampleRate) || sampleRate <= 0.0f)
	{
		LogF(L"OutProcVSTPlugin: invalid audio format, filter will bypass");
		return channelNames;
	}

    if(!analysisMode) {
        // The existing client accumulates callback fragments without waiting.
        // Keep IPC cadence independent of tiny APO callbacks; a 10 ms quantum
        // avoids thousands of kernel wakeups and sub-millisecond round trips.
        // This is explicit buffered transport latency, not a longer timeout.
        maxFrameCount=(std::max)(maxFrameCount,static_cast<unsigned>(std::ceil(double(sampleRate)/100.0)));
        this->maxFrameCount=maxFrameCount;
    }

	if (!OutProcAudioSharedMemorySize(channelCount, maxFrameCount, sharedByteCount))
	{
		LogF(L"OutProcVSTPlugin: invalid shared memory size, filter will bypass");
		return channelNames;
	}

    audioClient.prepare(channelCount,maxFrameCount,static_cast<unsigned>(sampleRate));
    // Interactive audio always uses the signed-in user's existing vendor profile.
    // Analysis clones already run in the requesting user's process context.
    brokerMode=!analysisMode;
    if(!analysisMode && !hostId.empty() && !guiUserSid.empty()) {
        auto endpoint=std::make_unique<EqApoControl::ControlEndpointOwner>();
        if(endpoint->create(hostId,guiUserSid,EqApoControl::controlIdentity(vstConfig.libraryPath,vstConfig.classUID,vstConfig.vst3ClassIndex),sampleRate,channelCount,false))controlEndpoint=std::move(endpoint);
    }
    transitionLaunchFailed.store(!startHost(),std::memory_order_release);
    if(analysisMode && sharedHeader && processHandle) {
        // AnalysisThread owns this clone. Its existing 10 s wall budget also
        // bounds startup; the live APO path never enters this control wait.
        // Do not send the first impulse to a component still loading its DLL.
        const auto until=GetTickCount64()+10000;
        while(!sharedHeader->ready.load(std::memory_order_acquire) && GetTickCount64()<until) {
            if(WaitForSingleObject(processHandle,2)!=WAIT_TIMEOUT)break;
        }
    }
    stopping.store(false);
    if(!analysisMode) supervisor=std::thread(&OutProcVSTPluginFilter::supervise,this);
    return channelNames;
}

#pragma AVRT_CODE_BEGIN
void OutProcVSTPluginFilter::process(double** output, double** input, unsigned frameCount)
{
    if(analysisMode) { processAnalysis(output,input,frameCount); return; }
    if(controlEndpoint)controlEndpoint->publishMonitorInput(input,frameCount);
    unsigned expected=0;
    if(realtimeGate.compare_exchange_strong(expected,1,std::memory_order_acquire)) {
        const auto before=sharedHeader ? sharedHeader->produced.load(std::memory_order_relaxed) : 0;
        audioClient.process(output,input,frameCount,sharedHeader);
        if(controlEndpoint && sharedHeader && sharedHeader->ready.load(std::memory_order_acquire)
            && sharedHeader->lastError.load(std::memory_order_acquire)!=4 && audioClient.readyForTransition())controlEndpoint->activate();
        if(sharedHeader && sharedHeader->produced.load(std::memory_order_relaxed)!=before && requestEvent) SetEvent(requestEvent);
        realtimeGate.store(0,std::memory_order_release);
    } else {
        // Supervisor never touches this audio-only timeline while it owns IPC.
        audioClient.process(output,input,frameCount,nullptr);
    }
}
#pragma AVRT_CODE_END

void OutProcVSTPluginFilter::closeHost()
{
    if(brokerSession){
        // Aliases belong to Owner, which signals only this graph's shutdown.
        brokerSession.reset();brokerCatalog.reset();
        if(processHandle){CloseHandle(processHandle);processHandle=nullptr;}
        sharedHeader=nullptr;mappingHandle=requestEvent=responseEvent=shutdownEvent=nullptr;
        return;
    }
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

    if(brokerMode)return startBrokerHost();

	if (namedHostMode)
		return startNamedHostSession();

	return startInheritedHost();
}

bool OutProcVSTPluginFilter::startBrokerHost(){
    auto session=std::make_unique<EqApoBroker::Owner>();
    GUID guid{};wchar_t text[64]{};
    if(FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid,text,64))return false;
    const std::wstring id=std::wstring(text).substr(1,36);
    const auto& user=guiUserSid;
    if(user.empty()){logLaunchFailure(L"no signed-in Windows user available for VST DSP");return false;}
    if(!session->create(id,user,vstConfig,static_cast<unsigned>(sampleRate),channelCount,maxFrameCount,
        controlEndpoint?hostId:L"",controlEndpoint?controlEndpoint->getGeneration():0,controlEndpoint?brokerInstanceId:L"")){
        logLaunchFailure(L"secured broker audio/state creation failed");return false;
    }
    auto catalog=std::make_unique<EqApoBroker::Catalog>();
    if(!catalog->open(session->space,user,true) || !catalog->publish(*session)){
        logLaunchFailure(L"broker catalog unavailable or full");return false;
    }
    mappingHandle=session->audio;requestEvent=session->request;responseEvent=session->response;shutdownEvent=session->shutdown;
    sharedHeader=session->header;sessionEpoch=session->metadata->generation;
    brokerSession=std::move(session);brokerCatalog=std::move(catalog);
    disabled=false;consecutiveTimeouts=0;return true;
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
    new(sharedHeader) OutProcAudioHeader();
	sharedHeader->magic = OUTPROC_AUDIO_MAGIC;
	sharedHeader->version = OUTPROC_AUDIO_VERSION;
	sharedHeader->sampleRate = static_cast<uint32_t>(sampleRate);
	sharedHeader->channelCount = channelCount;
	sharedHeader->maxFrames = maxFrameCount;
	sharedHeader->dspType = OUTPROC_AUDIO_DSP_VST;
    OutProcInitializeSlots(sharedHeader);
    sharedHeader->analysisOnly=analysisMode ? 1u : 0u;
    sharedHeader->sessionEpoch=++sessionEpoch;
    new(&sharedHeader->parameters) EqApoControl::ParameterMailbox(sessionEpoch);

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
    new(sharedHeader) OutProcAudioHeader();
	sharedHeader->magic = OUTPROC_AUDIO_MAGIC;
	sharedHeader->version = OUTPROC_AUDIO_VERSION;
	sharedHeader->sampleRate = static_cast<uint32_t>(sampleRate);
	sharedHeader->channelCount = channelCount;
	sharedHeader->maxFrames = maxFrameCount;
	sharedHeader->dspType = OUTPROC_AUDIO_DSP_VST;
    OutProcInitializeSlots(sharedHeader);
    sharedHeader->analysisOnly=analysisMode ? 1u : 0u;
    sharedHeader->sessionEpoch=++sessionEpoch;
    new(&sharedHeader->parameters) EqApoControl::ParameterMailbox(sessionEpoch);

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
		<< L" --shutdown " << reinterpret_cast<uintptr_t>(shutdownEvent)
		<< L" --vst-config \"" << configFilePath << L"\"";

    HANDLE nativeViewHandle=nullptr;
    if(controlEndpoint)DuplicateHandle(GetCurrentProcess(),controlEndpoint->getNativeViewHandle(),GetCurrentProcess(),&nativeViewHandle,0,TRUE,DUPLICATE_SAME_ACCESS);
    struct CloseViewHandle {HANDLE value;~CloseViewHandle(){if(value)CloseHandle(value);}} closeViewHandle{nativeViewHandle};
    if(nativeViewHandle)commandLine << L" --native-view " << reinterpret_cast<uintptr_t>(nativeViewHandle)
        << L" --session " << hostId;
	wstring commandLineStorage = commandLine.str();
	wstring workingDirectory = hostPath.substr(0, hostPath.find_last_of(L"\\/"));

    std::vector<HANDLE> inherited{mappingHandle,requestEvent,responseEvent,shutdownEvent};
    if(nativeViewHandle)inherited.push_back(nativeViewHandle);
	PROCESS_INFORMATION processInformation = {};

	if (!OutProcCreateRestrictedProcess(hostPath,commandLineStorage,workingDirectory,inherited,processInformation))
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
	if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;LS)(A;;GRGW;;;NS)(A;;GRGW;;;IU)", SDDL_REVISION_1, &securityDescriptor, NULL))
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

OutProcVSTPluginFilter::Diagnostics OutProcVSTPluginFilter::getDiagnostics() noexcept {
    Diagnostics result;
    result.launchFailed=transitionLaunchFailed.load(std::memory_order_acquire);
    // Sequential consistency pairs admission with the supervisor's replacement
    // barrier. Readers never acquire the callback's exclusive realtime gate.
    if(mappingChanging.load())return result;
    sessionReaders.fetch_add(1);
    if(mappingChanging.load()){sessionReaders.fetch_sub(1);return result;}
    if(sharedHeader) {
        result.available=true;
        result.processingQuantum=sharedHeader->maxFrames;
        result.hostAlive=processHandle && WaitForSingleObject(processHandle,0)==WAIT_TIMEOUT;
        result.hostPid=processHandle?GetProcessId(processHandle):0;
        result.ready=sharedHeader->ready.load(std::memory_order_acquire);
        result.error=sharedHeader->lastError.load(std::memory_order_acquire);
        result.heartbeat=sharedHeader->heartbeat.load(std::memory_order_acquire);
        result.consumed=sharedHeader->consumed.load(std::memory_order_acquire);
        result.produced=sharedHeader->produced.load(std::memory_order_acquire);
        result.missing=sharedHeader->underruns.load(std::memory_order_acquire);
        result.dropped=sharedHeader->overruns.load(std::memory_order_acquire);
        result.epoch=sharedHeader->sessionEpoch.load(std::memory_order_acquire);
        result.parameterGeneration=sharedHeader->parameters.generation;
        result.parameterSequence=sharedHeader->parameterSequence.load(std::memory_order_acquire);
        result.parameterDrops=sharedHeader->parameters.dropped.load(std::memory_order_acquire)+sharedHeader->parameterRejected.load(std::memory_order_acquire);
    }
    sessionReaders.fetch_sub(1);return result;
}

bool OutProcVSTPluginFilter::publishParameter(std::uint64_t generation,std::uint32_t id,double value,int offset) noexcept {
    if(mappingChanging.load() || parameterWriter.test_and_set(std::memory_order_acquire))return false;
    sessionReaders.fetch_add(1);
    if(mappingChanging.load()) {
        sessionReaders.fetch_sub(1);parameterWriter.clear(std::memory_order_release);return false;
    }
    bool accepted=false;
    // Control-plane bookkeeping only. Prepare replay state before publication
    // so an allocation failure cannot publish an edit that recovery forgets.
    if(sharedHeader)try {
        const auto key=L"#"+std::to_wstring(id);
        auto inserted=vstConfig.paramMap.try_emplace(key,float(value));
        const float previous=inserted.first->second;
        inserted.first->second=float(value);
        accepted=sharedHeader->parameters.publish(generation,id,value,offset);
        if(!accepted) {
            if(inserted.second)vstConfig.paramMap.erase(inserted.first);
            else inserted.first->second=previous;
        }
    }catch(...) {
        sharedHeader->parameterRejected.fetch_add(1,std::memory_order_relaxed);
    }
    if(accepted && requestEvent)SetEvent(requestEvent);
    sessionReaders.fetch_sub(1);parameterWriter.clear(std::memory_order_release);return accepted;
}

void OutProcVSTPluginFilter::supervise() {
    std::uint64_t lastHeartbeat=0; unsigned stalled=0;
    while(!stopping.load(std::memory_order_acquire)) {
        Sleep(50); // Control thread, never reachable from the APO callback.
        // Native GUI changes are already audible. Checkpoint them for worker
        // recovery without rebuilding the graph or touching the RT callback.
        if(controlEndpoint) {
            auto sequence=nativeRecoverySequence;std::wstring path;
            if(controlEndpoint->readNativeSnapshot(sequence,path)) {
                OutProcVSTConfig saved;
                if(OutProcReadVSTConfig(path,saved)
                    && EqApoControl::controlIdentity(saved.libraryPath,saved.classUID,saved.vst3ClassIndex)
                       ==EqApoControl::controlIdentity(vstConfig.libraryPath,vstConfig.classUID,vstConfig.vst3ClassIndex)
                    && !parameterWriter.test_and_set(std::memory_order_acquire)) {
                    vstConfig.chunkData=std::move(saved.chunkData);
                    vstConfig.paramMap=std::move(saved.paramMap);
                    nativeRecoverySequence=sequence;
                    parameterWriter.clear(std::memory_order_release);
                }
            }
        }
        if(brokerSession && !processHandle){
            const auto pid=brokerSession->metadata->workerPid.load(std::memory_order_acquire);
            if(pid){
                mappingChanging.store(true);
                if(sessionReaders.load()!=0)continue;
                processHandle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
                mappingChanging.store(false);
            }
        }
        if(controlEndpoint)for(unsigned count=0;count<256;++count) {
            if(!hasPendingControl)hasPendingControl=controlEndpoint->consume(pendingControl);
            if(!hasPendingControl)break;
            const auto diagnostics=getDiagnostics();
            if(!diagnostics.available || !publishParameter(diagnostics.parameterGeneration,pendingControl.id,pendingControl.normalized,pendingControl.sampleOffset))break;
            hasPendingControl=false;
        }
        const auto heartbeat=sharedHeader ? sharedHeader->heartbeat.load(std::memory_order_acquire) : 0;
        bool pendingAudio=false;
        if(sharedHeader)for(unsigned i=0;i<OUTPROC_AUDIO_SLOTS;++i) {
            const auto state=OutProcSlot(sharedHeader,i)->state.load(std::memory_order_acquire);
            pendingAudio |= state==SLOT_INPUT || state==SLOT_PROCESSING;
        }
        // An event-driven DSP worker intentionally has no idle heartbeat.
        // Require pending work before treating missing progress as a stall.
        stalled=pendingAudio && heartbeat==lastHeartbeat ? stalled+1 : 0; lastHeartbeat=heartbeat;
        const bool exited=!namedHostMode && processHandle && WaitForSingleObject(processHandle,0)==WAIT_OBJECT_0;
        if((exited || stalled>=100) && !namedHostMode && restartAttempts<3) {
            mappingChanging.store(true);
            // Let admitted readers finish without excluding the callback.
            // New readers return unavailable; no wait occurs on either plane.
            if(sessionReaders.load()!=0)continue;
            unsigned expected=0;
            if(!realtimeGate.compare_exchange_strong(expected,2,std::memory_order_acquire)) continue;
            ++restartAttempts;
            transitionLaunchFailed.store(!startHost(),std::memory_order_release);
            lastHeartbeat=0; stalled=0;
            realtimeGate.store(0,std::memory_order_release);
            mappingChanging.store(false);
            continue;
        }
        mappingChanging.store(false);
        if((exited || stalled>=100) && restartAttempts>=3 && sharedHeader) {
            sharedHeader->ready.store(0,std::memory_order_release);
            sharedHeader->lastError.store(4,std::memory_order_release); // failed retry budget, quarantine
        }
    }
}

void OutProcVSTPluginFilter::processAnalysis(double** output,double** input,unsigned frames) {
    // This clone exists only in AnalysisThread. The APO adapter never takes this path.
    bool completed=false;
    if(!analysisFailed.load(std::memory_order_acquire) && sharedHeader && frames<=maxFrameCount && requestEvent && responseEvent) {
        for(unsigned c=0;c<channelCount;++c) memcpy(OutProcAudioInput(sharedHeader)+std::size_t(c)*maxFrameCount,input[c],frames*sizeof(double));
        sharedHeader->frameCount=frames; sharedHeader->requestSeq=++requestSeq;
        sharedHeader->analysisRequest.store(requestSeq,std::memory_order_release);
        SetEvent(requestEvent);
        const auto result=WaitForSingleObject(responseEvent,2000);
        completed=result==WAIT_OBJECT_0 && sharedHeader->analysisComplete.load(std::memory_order_acquire)==requestSeq && sharedHeader->status==OUTPROC_AUDIO_STATUS_OK;
    }
    if(!completed) {
        analysisFailed.store(true,std::memory_order_release);
        if(sharedHeader)sharedHeader->lastError.store(5,std::memory_order_release);
        if(shutdownEvent)SetEvent(shutdownEvent);
    }
    for(unsigned c=0;c<channelCount;++c) {
        if(completed) memcpy(output[c],OutProcAudioOutput(sharedHeader)+std::size_t(c)*maxFrameCount,frames*sizeof(double));
        else memset(output[c],0,frames*sizeof(double));
    }
}
