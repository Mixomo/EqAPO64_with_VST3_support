//------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : Examples
// Filename    : public.sdk/samples/vst/transportcontrol/include/plugprocessor.h
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

#pragma once

#include "public.sdk/source/vst/utility/dataexchange.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstdataexchange.h"

namespace Steinberg {
namespace TransportControl {

static constexpr Vst::DataExchangeBlock InvalidDataExchangeBlock = {
    nullptr, 0, Vst::InvalidDataExchangeBlockID};

//-----------------------------------------------------------------------------
class PlugProcessor : public Vst::AudioEffect
{
public:
	PlugProcessor ();

	tresult PLUGIN_API initialize (FUnknown* context) SMTG_OVERRIDE;
	tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	tresult PLUGIN_API setActive (TBool state) SMTG_OVERRIDE;

	tresult PLUGIN_API setBusArrangements (Vst::SpeakerArrangement* inputs, int32 numIns,
	                                       Vst::SpeakerArrangement* outputs,
	                                       int32 numOuts) SMTG_OVERRIDE;

	tresult PLUGIN_API canProcessSampleSize (int32 symbolicSampleSize) SMTG_OVERRIDE;
	tresult PLUGIN_API setupProcessing (Vst::ProcessSetup& setup) SMTG_OVERRIDE;
	tresult PLUGIN_API process (Vst::ProcessData& data) SMTG_OVERRIDE;

	tresult PLUGIN_API connect (Vst::IConnectionPoint* other) SMTG_OVERRIDE;
	tresult PLUGIN_API disconnect (Vst::IConnectionPoint* other) SMTG_OVERRIDE;

	//--- ---------------------------------------------------------------------
	tresult PLUGIN_API setState (IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API getState (IBStream* state) SMTG_OVERRIDE;

	static FUnknown* createInstance (void*) { return (Vst::IAudioProcessor*)new PlugProcessor (); }

protected:
	template <typename SampleType>
	tresult processAudio (Vst::ProcessData& data);
	tresult (PlugProcessor::*processAudioPtr) (Vst::ProcessData& data);

	Vst::ProcessContext* getCurrentExchangeData ();

	std::unique_ptr<Vst::DataExchangeHandler> mDataExchangeHandler;
	int64 mLastExchangeBlockSendSystemTime {0};
	Vst::DataExchangeBlock mCurrentExchangeBlock {InvalidDataExchangeBlock};

	bool mBypass = false;
};

//------------------------------------------------------------------------
} // namespace TransportControl
} // namespace Steinberg
