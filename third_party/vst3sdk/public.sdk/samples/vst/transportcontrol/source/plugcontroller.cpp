//------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : Examples
// Filename    : public.sdk/samples/vst/transportcontrol/source/plugcontroller.cpp
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

#include "../include/plugcontroller.h"
#include "../include/plugids.h"
#include "../include/uitransportcontroller.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

using namespace VSTGUI;

namespace Steinberg {
namespace TransportControl {

//-----------------------------------------------------------------------------
tresult PLUGIN_API PlugController::initialize (FUnknown* context)
{
	tresult result = EditController::initialize (context);
	if (result == kResultTrue)
	{
		//---Create Parameters------------
		parameters.addParameter (STR16 ("Bypass"), nullptr, 1, 0,
		                         Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsBypass,
		                         TransportControlParams::kBypassId);

		parameters.addParameter (STR16 ("Playback"), nullptr, 1, 0,
		                         Vst::ParameterInfo::kIsReadOnly,
		                         TransportControlParams::kPlaybackId);
		parameters.addParameter (STR16 ("Recording"), nullptr, 1, 0,
		                         Vst::ParameterInfo::kIsReadOnly,
		                         TransportControlParams::kRecordingId);
		parameters.addParameter (STR16 ("CycleActive"), nullptr, 1, 0,
		                         Vst::ParameterInfo::kIsReadOnly,
		                         TransportControlParams::kCycleActiveId);
	}
	return kResultTrue;
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API PlugController::createView (const char* _name)
{
	std::string_view name (_name);
	if (name == Vst::ViewType::kEditor)
	{
		auto* view = new VST3Editor (this, "view", "plug.uidesc");
		return view;
	}
	return nullptr;
}

//------------------------------------------------------------------------
IController* PlugController::createSubController (UTF8StringPtr name,
                                                  const IUIDescription* /*description*/,
                                                  VST3Editor* /*editor*/)
{
	if (UTF8StringView (name) == "TransportController")
	{
		Vst::ITransportControl* transportControl = nullptr;
		hostContext->queryInterface (Vst::ITransportControl::iid, (void**)&transportControl);

		auto* controller = new Vst::UITransportController (transportControl);
		if (transportControl)
			transportControl->release ();
		return controller;
	}
	return nullptr;
}

//------------------------------------------------------------------------
tresult PLUGIN_API PlugController::setComponentState (IBStream* state)
{
	// we receive the current state of the component (processor part)
	// we read our parameters and bypass value...
	if (!state)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	// read the bypass
	int32 bypassState = 0;
	if (streamer.readInt32 (bypassState) == false)
		return kResultFalse;
	setParamNormalized (kBypassId, bypassState ? 1 : 0);

	return kResultOk;
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API PlugController::notify (Vst::IMessage* message)
{
	if (mDataExchange.onMessage (message))
		return kResultOk;

	return EditController::notify (message);
}

//------------------------------------------------------------------------
void PLUGIN_API PlugController::onDataExchangeBlocksReceived (
    Vst::DataExchangeUserContextID /*userContextID*/, uint32 /*numBlocks*/,
    Vst::DataExchangeBlock* block, TBool /*onBackgroundThread*/)
{
	// note that we should compensate the timing using a queue and the current systemTime before
	// updating the values!
	if (auto pc = reinterpret_cast<Vst::ProcessContext*> (block->data))
	{
		mProcessState = pc->state;

		Vst::ParamValue val = (mProcessState & Vst::ProcessContext::kPlaying) ? 1. : 0.;
		parameters.getParameter (TransportControlParams::kPlaybackId)->setNormalized (val);
		performEdit (TransportControlParams::kPlaybackId, val);

		val = (mProcessState & Vst::ProcessContext::kRecording) ? 1. : 0.;
		parameters.getParameter (TransportControlParams::kRecordingId)->setNormalized (val);
		performEdit (TransportControlParams::kRecordingId, val);

		val = (mProcessState & Vst::ProcessContext::kCycleActive) ? 1. : 0.;
		parameters.getParameter (TransportControlParams::kCycleActiveId)->setNormalized (val);
		performEdit (TransportControlParams::kCycleActiveId, val);
	}
}

//------------------------------------------------------------------------
} // namespace TransportControl
} // namespace Steinberg
