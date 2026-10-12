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

#include "stdafx.h"
#include <cmath>
#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/PrecisionTimer.h"
#include "VSTPluginFilter.h"

using namespace std;

static bool initializePluginSafely(VSTPluginInstance* instance) {
    __try { return instance->initialize(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool servicePluginSafely(VSTPluginInstance* instance) {
    __try { return instance->serviceRestartRequests(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool serviceControllerSafely(VSTPluginInstance* instance) {
    __try {instance->serviceControllerParameterChanges();return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

VSTPluginFilter::VSTPluginFilter(std::shared_ptr<VSTPluginLibrary> library, std::wstring chunkData, std::unordered_map<std::wstring, float> paramMap, int vst3ClassIndex, std::wstring hostId, std::wstring guiUserSid, std::wstring classUID)
	: library(library), chunkData(chunkData), paramMap(paramMap), vst3ClassIndex(vst3ClassIndex), hostId(hostId), guiUserSid(guiUserSid), classUID(classUID)
{
	libPath = library->getLibPath();
}

VSTPluginFilter::~VSTPluginFilter()
{
	cleanup();
}

std::vector<std::wstring> VSTPluginFilter::initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	cleanup();

	channelCount = channelNames.size();
	slowProcessingLimitSeconds = max(0.25, (static_cast<double>(maxFrameCount) / max(1.0f, sampleRate)) * 8.0);
	if (channelCount == 0)
		return channelNames;

	skipProcessing = false;

	void* mem = MemoryHelper::alloc(sizeof(VSTPluginInstance));
	VSTPluginInstance* firstEffect = new(mem) VSTPluginInstance(library, 2, vst3ClassIndex);
	if (!initializePluginSafely(firstEffect))
	{
		LogF(L"The VST plugin %s crashed during initialization.", libPath.c_str());
		skipProcessing = true;
	}

    if(!skipProcessing) {
        firstEffect->setUsedChannelCount(static_cast<int>(channelCount));
        firstEffect->writeToEffect(chunkData,paramMap);
        firstEffect->prepareForProcessing(sampleRate,maxFrameCount);
        if(!firstEffect->isProcessingValid()) skipProcessing=true;
    }
	effectChannelCount = max(firstEffect->numInputs(), firstEffect->numOutputs());
	if (effectChannelCount == 0)
	{
		LogF(L"The VST plugin %s does not expose audio inputs or outputs.", libPath.c_str());
		skipProcessing = true;
		effectChannelCount = static_cast<unsigned>(channelCount);
	}
	// round up
	effectCount = (channelCount + (effectChannelCount - 1)) / effectChannelCount;
	effects = (VSTPluginInstance**)MemoryHelper::alloc(effectCount * sizeof(VSTPluginInstance*));
	effects[0] = firstEffect;
	for (unsigned i = 1; i < effectCount; i++)
	{
		mem = MemoryHelper::alloc(sizeof(VSTPluginInstance));
		effects[i] = new(mem) VSTPluginInstance(library, 2, vst3ClassIndex);
		if (!initializePluginSafely(effects[i]) && !skipProcessing)
		{
			LogF(L"The VST plugin %s crashed during initialization.", libPath.c_str());
			skipProcessing = true;
		}
	}

	prepareForProcessing(sampleRate, maxFrameCount);

	// 2 times for input and output
	emptyChannelCount = 2 * (effectCount * effectChannelCount - channelCount);
	emptyChannels = (double**)MemoryHelper::alloc(emptyChannelCount * sizeof(double*));
	for (unsigned i = 0; i < emptyChannelCount; i++)
	{
		emptyChannels[i] = (double*)MemoryHelper::alloc(maxFrameCount * sizeof(double));
		memset(emptyChannels[i], 0, maxFrameCount * sizeof(double));
	}

	inputArray = (double**)MemoryHelper::alloc(64 * sizeof(double*));
	outputArray = (double**)MemoryHelper::alloc(64 * sizeof(double*));

	// Allocate float buffers for conversion
	if (firstEffect->numInputs() > 0) {
		floatInputs = (float**)MemoryHelper::alloc(64 * sizeof(float*));
		_floatInputBuffer = (float*)MemoryHelper::alloc(64 * maxFrameCount * sizeof(float));
		for (int i = 0; i < 64; ++i) {
			floatInputs[i] = _floatInputBuffer + i * maxFrameCount;
		}
	}

	if (firstEffect->numOutputs() > 0) {
		floatOutputs = (float**)MemoryHelper::alloc(64 * sizeof(float*));
		_floatOutputBuffer = (float*)MemoryHelper::alloc(64 * maxFrameCount * sizeof(float));
		for (int i = 0; i < 64; ++i) {
			floatOutputs[i] = _floatOutputBuffer + i * maxFrameCount;
		}
	}

	// The plugin already delays wet. Only the parallel dry path is delayed.
    maxFrames=maxFrameCount;
    delayBufferLength=(std::max)(0,firstEffect->getInitialDelay());
    historyCapacity=(std::max)(unsigned(sampleRate*10),unsigned((std::max)(1,firstEffect->getInitialDelay())))+maxFrameCount+1;
    reportedLatency.store(delayBufferLength);previousDryDelay=delayBufferLength;dryDelayBlend=1;
    warmupRemaining.store(delayBufferLength+2*maxFrameCount,std::memory_order_relaxed);
    dryHistory.assign(channelCount*historyCapacity,0); dryBlock.assign(channelCount*maxFrameCount,0);
    lastWet.assign(channelCount,0); dryPosition=0; wetBlend=0; fadeStep=1.0/(std::max)(1.0f,sampleRate/100.0f);
	// Legacy buffer members remain zero; no second delay is applied to wet.
    if(!skipProcessing && !hostId.empty() && !guiUserSid.empty()) {
        auto endpoint=std::make_unique<EqApoControl::ControlEndpointOwner>();
        const auto effectiveUID=classUID.empty() && library->isVST3()?library->getVST3ClassUID(vst3ClassIndex):classUID;
        if(endpoint->create(hostId,guiUserSid,EqApoControl::controlIdentity(libPath,effectiveUID,vst3ClassIndex),sampleRate,channelCount))
            controlEndpoint=std::move(endpoint);
    }
    stopping.store(false,std::memory_order_release);
    controlThread=std::thread(&VSTPluginFilter::serviceControl,this);

	return channelNames;
}

void VSTPluginFilter::prepareForProcessing(float sampleRate, unsigned maxFrameCount)
{
	__try
	{
		for (unsigned i = 0; i < effectCount; i++)
		{
			VSTPluginInstance* effect = effects[i];

			if (i == effectCount - 1 && (channelCount % effectChannelCount) != 0)
				effect->setUsedChannelCount(channelCount % effectChannelCount);
			else
				effect->setUsedChannelCount(effectChannelCount);
			// Instance zero was already restored and negotiated to determine
			// the channel layout. Repeating setup can reload large plugin assets.
			if(i!=0) {
				effect->writeToEffect(chunkData, paramMap);
				effect->prepareForProcessing(sampleRate, maxFrameCount);
			}
			effect->startProcessing();
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		LogF(L"The VST plugin %s crashed while preparing for processing.", libPath.c_str());
		skipProcessing = true;
	}
}

#pragma AVRT_CODE_BEGIN
void convertFloatToDouble(double* dest, const float* src, size_t count);

// Converts a block of doubles back to floats.
void convertDoubleToFloat(float* dest, const double* src, size_t count);

void VSTPluginFilter::process(double** output, double** input, unsigned frameCount)
{
    if(controlEndpoint)controlEndpoint->publishMonitorInput(input,frameCount);
    if(frameCount>maxFrames || !historyCapacity) {
        for(unsigned c=0;c<channelCount;++c)
            if(output[c]!=input[c])std::memcpy(output[c],input[c],frameCount*sizeof(double));
        return;
    }
    const unsigned updatedLatency=reportedLatency.load(std::memory_order_acquire);
    if(updatedLatency!=delayBufferLength && updatedLatency+maxFrames<historyCapacity) {
        previousDryDelay=delayBufferLength;delayBufferLength=updatedLatency;dryDelayBlend=0;
    }
    const unsigned latency=delayBufferLength;
    for(unsigned f=0;f<frameCount;++f,++dryPosition) for(unsigned c=0;c<channelCount;++c) {
        const auto base=std::size_t(c)*historyCapacity;
        dryHistory[base+dryPosition%historyCapacity]=input[c][f];
        const double newDry=dryPosition>=latency ? dryHistory[base+(dryPosition-latency)%historyCapacity] : 0;
        const double oldDry=dryPosition>=previousDryDelay ? dryHistory[base+(dryPosition-previousDryDelay)%historyCapacity] : 0;
        const double factor=(std::min)(1.0,dryDelayBlend+f*fadeStep);
        dryBlock[std::size_t(c)*maxFrames+f]=oldDry+(newDry-oldDry)*factor;
    }
    const bool changingLatency=dryDelayBlend<1;
    dryDelayBlend=(std::min)(1.0,dryDelayBlend+frameCount*fadeStep);
    unsigned expected=0;
    const bool ownsProcessor=processingGate.compare_exchange_strong(expected,1,std::memory_order_acquire);
    if(skipProcessing || !ownsProcessor) {
        for(unsigned f=0;f<frameCount;++f) {
            wetBlend=(std::max)(0.0,wetBlend-fadeStep);
            for(unsigned c=0;c<channelCount;++c) {
                const auto dry=dryBlock[std::size_t(c)*maxFrames+f];
                output[c][f]=dry+(lastWet[c]-dry)*wetBlend;
            }
        }
        if(ownsProcessor) processingGate.store(0,std::memory_order_release);
        return;
    }

	PrecisionTimer processingTimer;
	processingTimer.start();

	__try
	{
		unsigned channelOffset = 0;
        // Mapped once during initialization. Bounded dequeue and SDK edits
        // run under DSP ownership; no kernel lookup, allocation or GUI call.
        EqApoControl::ParameterEvent edit;
        for(unsigned n=0;controlEndpoint && n<EqApoControl::ParameterMailbox::capacity && controlEndpoint->consume(edit);++n) {
            for(unsigned instance=0;instance<effectCount;++instance)
                effects[instance]->applyParameterChangeOnDsp(edit.id,edit.normalized,edit.sampleOffset);
            appliedParameterSequence.store(edit.sequence,std::memory_order_release);
        }
		unsigned emptyChannelIndex = 0;
		for (unsigned i = 0; i < effectCount; i++)
		{
			VSTPluginInstance* effect = effects[i];
			// Setup double pointer arrays to point to the correct source/destination double buffers
			for (int j = 0; j < effect->numInputs(); j++)
			{
				if (channelOffset + j < channelCount)
					inputArray[j] = input[channelOffset + j];
				else
					inputArray[j] = emptyChannels[emptyChannelIndex++];
			}

			for (int j = 0; j < effect->numOutputs(); j++)
			{
				if (channelOffset + j < channelCount)
					outputArray[j] = output[channelOffset + j];
				else
					outputArray[j] = emptyChannels[emptyChannelIndex++];
			}

			if (effect->canDoubleReplacing()) {
				effect->processDoubleReplacing(inputArray, outputArray, frameCount);
                if(!effect->isProcessingValid()) skipProcessing=true;
			}
			else {
				// Convert input from double** to float** using pre-allocated buffers
				for (int j = 0; j < effect->numInputs(); j++)
				{
					convertDoubleToFloat(floatInputs[j], inputArray[j], frameCount);
				}

				if (effect->canReplacing())
				{
					effect->processReplacing(floatInputs, floatOutputs, frameCount);
                    if(!effect->isProcessingValid()) skipProcessing=true;
				}
				else
				{
					// For non-replacing, VST expects to add to the output. Clear float buffer first.
					for (int j = 0; j < effect->numOutputs(); j++)
						memset(floatOutputs[j], 0, frameCount * sizeof(float));
					effect->process(floatInputs, floatOutputs, frameCount);
				}

				// Convert output from float** back to double** into the final destination
				for (int j = 0; j < effect->numOutputs(); j++)
				{
					convertFloatToDouble(outputArray[j], floatOutputs[j], frameCount);
				}
			}

			if (effect->numOutputs() < effect->numInputs())
			{
				for (int j = effect->numOutputs(); j < effect->numInputs(); j++)
				{
					if (channelOffset + j < channelCount)
						memset(output[channelOffset + j], 0, frameCount * sizeof(double));
				}
			}

			channelOffset += effectChannelCount;
		}


	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
        reportCrash=false; skipProcessing=true;
    }
    const auto remaining=warmupRemaining.load(std::memory_order_relaxed);
    const bool warming=remaining!=0;
    if(remaining)warmupRemaining.store(remaining-(std::min)(remaining,frameCount),std::memory_order_relaxed);
    for(unsigned f=0;f<frameCount;++f) {
        wetBlend=(std::max)(0.0,(std::min)(1.0,wetBlend+((skipProcessing || changingLatency || warming) ? -fadeStep : fadeStep)));
        for(unsigned c=0;c<channelCount;++c) {
            const double dry=dryBlock[std::size_t(c)*maxFrames+f];
            double wet=(skipProcessing || warming) ? lastWet[c] : output[c][f];
            if(!std::isfinite(wet)) { wet=lastWet[c]; skipProcessing=true; }
            output[c][f]=dry+(wet-dry)*wetBlend;
            if(!skipProcessing && !warming) lastWet[c]=wet;
        }
    }

	const double elapsedSeconds = processingTimer.stop();
	if (elapsedSeconds > slowProcessingLimitSeconds)
	{
		reportSlowProcessing=false;
		skipProcessing = true;
	}
    processingGate.store(0,std::memory_order_release);
}

std::shared_ptr<VSTPluginLibrary> VSTPluginFilter::getLibrary() const
{
	return library;
}

std::wstring VSTPluginFilter::getChunkData() const
{
	return chunkData;
}

std::unordered_map<std::wstring, float> VSTPluginFilter::getParamMap() const
{
	return paramMap;
}

int VSTPluginFilter::getVST3ClassIndex() const
{
	return vst3ClassIndex;
}

void VSTPluginFilter::cleanup()
{
    stopping.store(true,std::memory_order_release);
    if(controlThread.joinable()) controlThread.join();
    controlEndpoint.reset();
    appliedParameterSequence.store(0,std::memory_order_relaxed);
	if (effects != NULL)
	{
		for (unsigned i = 0; i < effectCount; i++)
		{
			VSTPluginInstance* effect = effects[i];
			bool destroyed = false;
			__try
			{
				effect->stopProcessing();
				effect->~VSTPluginInstance();
				destroyed = true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogF(L"The VST plugin %s crashed while being unloaded.", libPath.c_str());
			}
			// A failed native teardown can leave plugin callbacks referencing the
			// host object. Quarantine its allocation instead of creating a UAF.
			if (destroyed) MemoryHelper::free(effect);
		}
		MemoryHelper::free(effects);
		effects = NULL;
	}
	effectCount = 0;

	if (emptyChannels != NULL)
	{
		for (unsigned i = 0; i < emptyChannelCount; i++)
			MemoryHelper::free(emptyChannels[i]);
		MemoryHelper::free(emptyChannels);
		emptyChannels = NULL;
	}
	emptyChannelCount = 0;

	if (inputArray != NULL)
	{
		MemoryHelper::free(inputArray);
		inputArray = NULL;
	}

	if (outputArray != NULL)
	{
		MemoryHelper::free(outputArray);
		outputArray = NULL;
	}
    
    if (floatInputs != NULL) {
		MemoryHelper::free(floatInputs);
		floatInputs = NULL;
	}
	if (_floatInputBuffer != NULL) {
		MemoryHelper::free(_floatInputBuffer);
		_floatInputBuffer = NULL;
	}
	if (floatOutputs != NULL) {
		MemoryHelper::free(floatOutputs);
		floatOutputs = NULL;
	}
	if (_floatOutputBuffer != NULL) {
		MemoryHelper::free(_floatOutputBuffer);
		_floatOutputBuffer = NULL;
	}

	if (delayBuffers != NULL)
	{
		for (unsigned i = 0; i < channelCount; i++)
			MemoryHelper::free(delayBuffers[i]);
		MemoryHelper::free(delayBuffers);
		delayBuffers = NULL;
	}
	if (delayTempBuffer != NULL)
	{
		MemoryHelper::free(delayTempBuffer);
		delayTempBuffer = NULL;
	}
	delayBufferLength = 0;
	delayBufferOffset = 0;
}

void VSTPluginFilter::serviceControl() {
    while(!stopping.load(std::memory_order_acquire)) {
        Sleep(10);
        bool pending=false;
        // Meter/controller output is independent of processor ownership.
        // Taking the DSP gate for every meter update periodically substitutes
        // dry audio, especially during rapid controls or analysis rendering.
        for(unsigned i=0;i<effectCount;++i) {
            if(!serviceControllerSafely(effects[i]))skipProcessing.store(true,std::memory_order_release);
            pending|=effects[i]->pendingRestartMask()!=0;
        }
        if(!pending) continue;
        unsigned expected=0;
        if(!processingGate.compare_exchange_strong(expected,2,std::memory_order_acquire)) continue;
        for(unsigned i=0;i<effectCount && !skipProcessing;++i) {
            const auto flags=effects[i]->pendingRestartMask();
            if(!servicePluginSafely(effects[i]) ||
               effects[i]->numInputs()>int(effectChannelCount) || effects[i]->numOutputs()>int(effectChannelCount) ||
               effects[i]->getInitialDelay()<0 || unsigned(effects[i]->getInitialDelay())+maxFrames>=historyCapacity) {
                // Existing storage and timeline stay intact until a new graph is prepared.
                // Unsupported structural changes must not write beyond the negotiated buffers.
                skipProcessing.store(true,std::memory_order_release);
            }
            else if(flags & (Steinberg::Vst::kReloadComponent|Steinberg::Vst::kIoChanged|Steinberg::Vst::kLatencyChanged))
                warmupRemaining.store(unsigned((std::max)(0,effects[i]->getInitialDelay()))+2*maxFrames,std::memory_order_relaxed);
        }
        if(effectCount && !skipProcessing) reportedLatency.store(unsigned(effects[0]->getInitialDelay()),std::memory_order_release);
        processingGate.store(0,std::memory_order_release);
    }
}
