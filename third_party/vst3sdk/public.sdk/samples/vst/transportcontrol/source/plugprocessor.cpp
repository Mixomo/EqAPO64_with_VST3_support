//------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : Examples
// Filename    : public.sdk/samples/vst/transportcontrol/source/plugprocessor.cpp
// Created by  : Steinberg, 05/2026
// Description : Transport Control Example for VST 3
//
//-----------------------------------------------------------------------------
// This file is part of a Steinberg SDK. It is subject to the license terms
// in the LICENSE file found in the top-level directory of this distribution
// and at www.steinberg.net/sdklicenses.
// No part of the SDK, including this file, may be copied, modified, propagated,
// or distributed except according to the terms contained in the LICENSE file.
//-----------------------------------------------------------------------------

#include "../include/plugprocessor.h"
#include "../include/plugids.h"

#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

namespace Steinberg {
namespace TransportControl {

static constexpr int64 kRefreshRateForExchangePC = 40000000; // 25Hz

//-----------------------------------------------------------------------------
PlugProcessor::PlugProcessor ()
{
	// register its editor class
	setControllerClass (MyControllerUID);

	// default init
	processAudioPtr = &PlugProcessor::processAudio<float>;

	// needed for the process context information exchange with the host
	processContextRequirements.needSystemTime ();
	processContextRequirements.needTransportState ();
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API PlugProcessor::initialize (FUnknown* context)
{
	//---always initialize the parent-------
	tresult result = AudioEffect::initialize (context);
	if (result != kResultTrue)
		return kResultFalse;

	//---create Audio In/Out busses------
	// we want a Stereo Input and a Stereo Output
	addAudioInput (STR16 ("AudioInput"), Vst::SpeakerArr::kStereo);
	addAudioOutput (STR16 ("AudioOutput"), Vst::SpeakerArr::kStereo);

	mDataExchangeHandler = std::make_unique<Vst::DataExchangeHandler> (
	    this, [this] (auto& config, const auto& /*setup*/) {
		    config.numBlocks = 5;
		    config.blockSize = sizeof (Vst::ProcessContext);
		    return true;
	    });

	return kResultTrue;
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API PlugProcessor::terminate ()
{
	mDataExchangeHandler = nullptr;
	return AudioEffect::terminate ();
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API PlugProcessor::setActive (TBool state)
{
	if (mDataExchangeHandler)
	{
		if (state)
			mDataExchangeHandler->onActivate (processSetup);
		else
			mDataExchangeHandler->onDeactivate ();
	}
	return AudioEffect::setActive (state);
}

//------------------------------------------------------------------------
tresult PLUGIN_API PlugProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
	return ((symbolicSampleSize == Vst::kSample32) || (symbolicSampleSize == Vst::kSample64)) ?
	           kResultTrue :
	           kResultFalse;
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API PlugProcessor::setBusArrangements (Vst::SpeakerArrangement* inputs, int32 numIns,
                                                      Vst::SpeakerArrangement* outputs,
                                                      int32 numOuts)
{
	// we only support stereo to stereo
	if (numIns == 1 && numOuts == 1 && inputs[0] == Vst::SpeakerArr::kStereo &&
	    outputs[0] == Vst::SpeakerArr::kStereo)
	{
		return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
	}
	return kResultFalse;
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API PlugProcessor::setupProcessing (Vst::ProcessSetup& setup)
{
	if (setup.symbolicSampleSize == Vst::kSample64)
		processAudioPtr = &PlugProcessor::processAudio<double>;
	else
		processAudioPtr = &PlugProcessor::processAudio<float>;

	return AudioEffect::setupProcessing (setup);
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API PlugProcessor::connect (IConnectionPoint* other)
{
	auto res = AudioEffect::connect (other);
	if (mDataExchangeHandler)
		mDataExchangeHandler->onConnect (other, getHostContext ());
	return res;
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API PlugProcessor::disconnect (IConnectionPoint* other)
{
	if (mDataExchangeHandler)
		mDataExchangeHandler->onDisconnect (other);

	return AudioEffect::disconnect (other);
}

//-----------------------------------------------------------------------------
Vst::ProcessContext* PlugProcessor::getCurrentExchangeData ()
{
	if (!mDataExchangeHandler)
		return nullptr;

	auto block = mDataExchangeHandler->getCurrentOrNewBlock ();
	if (block.blockID == Vst::InvalidDataExchangeBlockID)
		return nullptr;
	if (mCurrentExchangeBlock != block)
	{
		mCurrentExchangeBlock = block;
	}
	return reinterpret_cast<Vst::ProcessContext*> (mCurrentExchangeBlock.data);
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API PlugProcessor::process (Vst::ProcessData& data)
{
	//--- Read inputs parameter changes-----------
	if (data.inputParameterChanges)
	{
		int32 numParamsChanged = data.inputParameterChanges->getParameterCount ();
		for (int32 index = 0; index < numParamsChanged; index++)
		{
			if (Vst::IParamValueQueue* paramQueue =
			        data.inputParameterChanges->getParameterData (index))
			{
				Vst::ParamValue value;
				int32 sampleOffset;
				int32 numPoints = paramQueue->getPointCount ();
				switch (paramQueue->getParameterId ())
				{
					case TransportControlParams::kBypassId:
						if (paramQueue->getPoint (numPoints - 1, sampleOffset, value) ==
						    kResultTrue)
							mBypass = (value > 0.5f);
						break;
				}
			}
		}
	}

	if (data.processContext)
	{
		if (mDataExchangeHandler)
		{
			if (!mDataExchangeHandler->isEnabled ())
				mDataExchangeHandler->enable (true);

			if (data.processContext->systemTime - mLastExchangeBlockSendSystemTime >
			    kRefreshRateForExchangePC)
			{
				mLastExchangeBlockSendSystemTime = data.processContext->systemTime;
				if (auto pc = getCurrentExchangeData ())
				{
					memcpy (pc, data.processContext, sizeof (Vst::ProcessContext));
					mDataExchangeHandler->sendCurrentBlock ();
				}
				else
					mDataExchangeHandler->discardCurrentBlock ();
			}
		}
	}

	//--- Process Audio---------------------
	//--- ----------------------------------
	if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples == 0)
	{
		// nothing to do
		return kResultOk;
	}

	return (this->*processAudioPtr) (data);
}

//------------------------------------------------------------------------
template <typename SampleType>
tresult PlugProcessor::processAudio (Vst::ProcessData& data)
{
	int32 numFrames = data.numSamples;

	uint32 sampleFramesSize = getSampleFramesSizeInBytes (processSetup, numFrames);
	auto** currentInputBuffers =
	    (SampleType**)Vst::getChannelBuffersPointer (processSetup, data.inputs[0]);
	auto** currentOutputBuffers =
	    (SampleType**)Vst::getChannelBuffersPointer (processSetup, data.outputs[0]);

	// if we have only silence clear the output and do nothing.
	data.outputs->silenceFlags = data.inputs->silenceFlags ? 0x7FFFF : 0;
	if (data.inputs->silenceFlags)
	{
		memset (currentOutputBuffers[0], 0, sampleFramesSize);
		memset (currentOutputBuffers[1], 0, sampleFramesSize);

		return kResultOk;
	}

	memcpy (currentOutputBuffers[0], currentInputBuffers[0], sampleFramesSize);
	memcpy (currentOutputBuffers[1], currentInputBuffers[1], sampleFramesSize);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API PlugProcessor::setState (IBStream* state)
{
	if (!state)
		return kResultFalse;

	// called when we load a preset or project, the model has to be reloaded

	IBStreamer streamer (state, kLittleEndian);

	int32 savedBypass = 0;
	if (streamer.readInt32 (savedBypass) == false)
		return kResultFalse;

	mBypass = savedBypass > 0;

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API PlugProcessor::getState (IBStream* state)
{
	// here we need to save the model (preset or project)

	int32 toSaveBypass = mBypass ? 1 : 0;

	IBStreamer streamer (state, kLittleEndian);
	streamer.writeInt32 (toSaveBypass);

	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace TransportControl
} // namespace Steinberg
