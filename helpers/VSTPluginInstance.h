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

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include "aeffectx.h"
#include "RealtimeVST.h"
#include <atomic>
#include <mutex>
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/gui/iplugview.h"

class VSTPluginLibrary;

class VSTPluginInstance
{
public:
	VSTPluginInstance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel, int vst3ClassIndex = 0);
	~VSTPluginInstance();
    static std::uint64_t getLiveInstanceCount() noexcept;
    static Steinberg::FUnknown* createFactoryHostContext();

	bool initialize();

	int numInputs() const;
	int numOutputs() const;
	bool canReplacing() const;
	int uniqueID() const;
	std::wstring getName() const;
	int getUsedChannelCount() const;
	void setUsedChannelCount(int count);
	float getSampleRate() const;
	int getProcessLevel() const;
	void setProcessLevel(int value);
	int getLanguage() const;
	void setLanguage(int value);
	bool canDoubleReplacing() const;
	int getInitialDelay() const;
    bool isProcessingValid() const noexcept { return processingSetupValid && stateRestoreValid; }
    bool wasStateRestored() const noexcept { return stateRestoreValid; }
	vst_time_info* getTimeInfo() noexcept { vstTime.sampleRate=sampleRate; return &vstTime; }
	bool serviceRestartRequests();
    void serviceControllerParameterChanges();
    std::uint32_t pendingRestartMask() const noexcept {return restartFlags.load(std::memory_order_acquire);}
    bool hasControlRequests() const noexcept {return restartFlags.load(std::memory_order_acquire)!=0 || latestControllerInputs.hasDirty() || outputMailboxPending.load(std::memory_order_acquire) || !outputVST3ParameterChanges.empty();}
	void setEditorScale(float scale);
	std::uint64_t getStateGeneration() const noexcept { return stateGeneration.load(); }
    struct ParameterMetadata {
        std::uint32_t id;
        std::wstring title;
        double normalizedValue;
        std::int32_t flags;
        std::int32_t stepCount=0;
    };
    // Control/scanner thread only: never query a controller from audio.
    std::vector<ParameterMetadata> getParameterMetadata() const;
    // Non-mutating VST3 controller formatting; control/scanner thread only.
    std::wstring getParameterValueText(std::uint32_t id,double normalizedValue) const;
    std::wstring getCurrentParameterText(std::uint32_t id) const;

	void prepareForProcessing(float sampleRate, int blockSize, bool preferDouble = true);
	void writeToEffect(const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap);
	void readFromEffect(std::wstring& chunkData, std::unordered_map<std::wstring, float>& paramMap) const;
	bool getVST3BusArrangements(std::vector<Steinberg::Vst::SpeakerArrangement>& inputs,
		std::vector<Steinberg::Vst::SpeakerArrangement>& outputs) const;
	bool setVST3BusArrangements(const std::vector<Steinberg::Vst::SpeakerArrangement>& inputs,
		const std::vector<Steinberg::Vst::SpeakerArrangement>& outputs);

	void startProcessing();
	void processDoubleReplacing(double** inputArray, double** outputArray, int frameCount);
	void processReplacing(float** inputArray, float** outputArray, int frameCount);
	void process(float** inputArray, float** outputArray, int frameCount);
	void stopProcessing();
    // Only a separate editor's optional input mirror uses this mutex. The
    // production APO processor never takes it in its realtime callback.
    std::recursive_mutex& editorMirrorMutex() const {return mirrorMutex;}

	bool startEditing(HWND hWnd, short* width, short* height);
	bool editorSupportsUserResize() const;
	bool resizeEditorView(int width, int height);
    bool hasActiveEditorGesture() const noexcept {return activeEditGestures.load(std::memory_order_acquire)>0;}
	void doIdle();
	void stopEditing();
    void noteEditorInteraction() noexcept { editorInteraction.store(true,std::memory_order_release); }
    void requestLegacyIdle() noexcept { legacyIdleRequested.store(true,std::memory_order_release); }
    bool hasLegacyIdleRequest() const noexcept { return legacyIdleRequested.load(std::memory_order_acquire); }

	void setAutomateFunc(std::function<void()> func);
	void onAutomate();
    void setParameterSink(std::function<void(std::uint32_t,double,int)> sink,bool routeThroughSink=false);
    void notifyParameterEdit(std::uint32_t id,double value,int offset=0);

	void setSizeWindowFunc(std::function<void(int, int)> func);
	void onSizeWindow(int w, int h);
	bool getEditorSize(int* width, int* height);
    // One control producer; events are applied at a bounded offset in the next block.
    void queueVST3ParameterChange(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value, int sampleOffset=0);
    // Called only by the DSP owner; VST2 edits use block boundaries.
    bool applyParameterChangeOnDsp(std::uint32_t id,double value,int sampleOffset);

private:
    mutable std::recursive_mutex mirrorMutex;
    std::unordered_map<std::uint32_t,double> notifiedParameterValues;
	class VST3HostContext;
	class VST3MemoryStream;

	bool initializeVST2();
	bool initializeVST3();
	void releaseVST3();
	void configureVST3Buses(int requestedChannelCount);
	Steinberg::Vst::SpeakerArrangement speakerArrangementForChannelCount(int count) const;

	std::shared_ptr<VSTPluginLibrary> library;
	vst_effect_t* effect = NULL;
	Steinberg::Vst::IComponent* vst3Component = NULL;
	Steinberg::Vst::IAudioProcessor* vst3Processor = NULL;
	Steinberg::Vst::IEditController* vst3Controller = NULL;
	Steinberg::Vst::IConnectionPoint* vst3ComponentConnection = NULL;
	Steinberg::Vst::IConnectionPoint* vst3ControllerConnection = NULL;
	Steinberg::IPlugView* vst3View = NULL;
	HWND vst3EditorHostWindow = NULL;
    std::atomic<bool> editorInteraction{false};
    std::atomic<bool> legacyIdleRequested{false};
	VST3HostContext* vst3HostContext = NULL;
	int vst3InputMainIndex=-1,vst3OutputMainIndex=-1;
	int vst3InputBusCount = 0;
	int vst3OutputBusCount = 0;
	int vst3InputChannelCount = 0;
	int vst3OutputChannelCount = 0;
	// The requested device shape is negotiated once per shape transition. A
	// prepare/restart alone must not overwrite a layout selected by the plugin.
	int vst3NegotiatedChannelCount = -1;
	bool vst3SupportsDouble = false;
    bool preferDoubleProcessing=true;
    bool stateRestoreValid=true;
	bool vst3Active = false;
	bool vst3Processing = false;
	Steinberg::Vst::ProcessContext vst3ProcessContext = {};
	Steinberg::Vst::TSamples vst3SamplePosition = 0;
	EqApoRT::SpscRing<EqApoRT::ParameterEvent,4097> pendingVST3ParameterChanges;
    EqApoRT::SpscRing<EqApoRT::ParameterEvent,4097> outputVST3ParameterChanges;
    EqApoRT::ParameterChanges vst3InputChanges, vst3OutputChanges;
    EqApoRT::LatestParameterValues latestInputParameters, latestOutputParameters;
    EqApoRT::LatestParameterValues latestControllerInputs;
    std::atomic<bool> outputMailboxPending{false};
    std::atomic<std::uint32_t> restartFlags{0};
    std::atomic<std::uint64_t> stateGeneration{0}, parameterOverflows{0};
    std::atomic<unsigned> activeEditGestures{0};
    vst_time_info vstTime{};
    int preparedBlockSize=0;
    bool controllerIsComponent=false;
    bool resizingEditor=false;
    float editorScale=0;
    std::atomic<bool> automationNotification{false};
    std::atomic<std::uint64_t> pendingEditorSize{0};
    bool processingSetupValid=true;
    std::vector<Steinberg::Vst::AudioBusBuffers> vst3InputBuffers,vst3OutputBuffers;
    std::vector<Steinberg::Vst::BusInfo> vst3InputBusInfo,vst3OutputBusInfo;
	std::function<void()> automateFunc;
    std::function<void(std::uint32_t,double,int)> parameterSink;
    std::atomic<bool> routeEditsThroughSink{false};
	std::function<void(int, int)> sizeWindowFunc;
	float sampleRate = 0.0f;
	int usedChannelCount = -1;
	int processLevel = 0;
	int language = 1;
	int vst3ClassIndex = 0;
};
