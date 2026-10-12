/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include "IFilter.h"
#include <thread>
#include <atomic>
#include "helpers/VSTPluginLibrary.h"
#include "outproc/OutProcControlEndpoint.h"

#pragma AVRT_VTABLES_BEGIN
class VSTPluginFilter : public IFilter
{
public:
	VSTPluginFilter(std::shared_ptr<VSTPluginLibrary> library, std::wstring chunkData, std::unordered_map<std::wstring, float> paramMap, int vst3ClassIndex = 0, std::wstring hostId = L"", std::wstring guiUserSid = L"", std::wstring classUID = L"");
	~VSTPluginFilter();

	bool getInPlace() override {return false;}
    unsigned getLatencySamples() const noexcept override {return reportedLatency.load(std::memory_order_acquire);}
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void prepareForProcessing(float sampleRate, unsigned maxFrameCount);
	void process(double** output, double** input, unsigned frameCount) override;

	std::shared_ptr<VSTPluginLibrary> getLibrary() const;
	std::wstring getChunkData() const;
	std::unordered_map<std::wstring, float> getParamMap() const;
	int getVST3ClassIndex() const;
    const std::wstring& getHostId() const {return hostId;}
    const std::wstring& getClassUID() const {return classUID;}
    std::uint64_t getAppliedParameterSequence() const noexcept {return appliedParameterSequence.load(std::memory_order_acquire);}
    // Control-thread snapshots; graph lifetime must remain stable.
    std::uint64_t getParameterGeneration() const noexcept {return controlEndpoint?controlEndpoint->getGeneration():0;}
    std::uint64_t getParameterDrops() const noexcept {return controlEndpoint?controlEndpoint->getDropped():0;}

private:
	void cleanup();

	std::shared_ptr<VSTPluginLibrary> library;
	std::wstring libPath;
	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	int vst3ClassIndex = 0;
    std::wstring hostId,guiUserSid,classUID;
    std::unique_ptr<EqApoControl::ControlEndpointOwner> controlEndpoint;
    std::atomic<std::uint64_t> appliedParameterSequence{0};
	size_t channelCount;
	unsigned effectChannelCount;
	size_t effectCount = 0;
	VSTPluginInstance** effects = NULL;
	size_t emptyChannelCount = 0;
	double** emptyChannels = NULL;
	double** inputArray = NULL;
	double** outputArray = NULL;

	// Buffers for float conversion
	float** floatInputs = NULL;
	float* _floatInputBuffer = NULL;
	float** floatOutputs = NULL;
	float* _floatOutputBuffer = NULL;

	// Delay compensation buffers
	unsigned delayBufferLength = 0;
	double** delayBuffers = NULL;
	double* delayTempBuffer = NULL;
	unsigned delayBufferOffset = 0;

    std::vector<double> dryHistory,dryBlock,lastWet;
    unsigned historyCapacity=0,maxFrames=0,previousDryDelay=0;
    double dryDelayBlend=1;
    std::atomic<unsigned> reportedLatency{0};
    std::uint64_t dryPosition=0;
    double wetBlend=0,fadeStep=1;
    std::atomic<bool> skipProcessing{false};
    std::atomic<bool> stopping{false};
    std::atomic<unsigned> processingGate{0};
    std::atomic<unsigned> warmupRemaining{0};
    std::thread controlThread;
    void serviceControl();
	bool reportCrash = true;
	bool reportSlowProcessing = true;
	double slowProcessingLimitSeconds = 0.25;
};
#pragma AVRT_VTABLES_END
