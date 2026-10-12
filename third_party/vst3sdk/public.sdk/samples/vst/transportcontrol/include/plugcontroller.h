///------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : Examples
// Filename    : public.sdk/samples/vst/transportcontrol/include/plugcontroller.h
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

#include "vstgui/plugin-bindings/vst3editor.h"
#include "public.sdk/source/vst/utility/dataexchange.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/vst/ivstdataexchange.h"

namespace Steinberg {
namespace TransportControl {

//-----------------------------------------------------------------------------
class PlugController : public Vst::EditController,
                       public VSTGUI::VST3EditorDelegate,
                       public Vst::IDataExchangeReceiver
{
public:
	using UTF8StringPtr = VSTGUI::UTF8StringPtr;
	using IUIDescription = VSTGUI::IUIDescription;
	using IController = VSTGUI::IController;
	using VST3Editor = VSTGUI::VST3Editor;

//------------------------------------------------------------------------
	// create function required for plug-in factory,
	// it will be called to create new instances of this controller
//------------------------------------------------------------------------
	static FUnknown* createInstance (void*) { return (Vst::IEditController*)new PlugController (); }

	//---from IPluginBase--------
	tresult PLUGIN_API initialize (FUnknown* context) SMTG_OVERRIDE;

	//---from EditController-----
	IPlugView* PLUGIN_API createView (const char* name) SMTG_OVERRIDE;
	tresult PLUGIN_API notify (Vst::IMessage* message) SMTG_OVERRIDE;
	IController* createSubController (UTF8StringPtr name, const IUIDescription* /*description*/,
	                                  VST3Editor* /*editor*/) SMTG_OVERRIDE;
	tresult PLUGIN_API setComponentState (IBStream* state) SMTG_OVERRIDE;

	//---IDataExchangeReceiver----------------------------
	void PLUGIN_API queueOpened (Vst::DataExchangeUserContextID userContextID, uint32 blockSize,
	                             TBool& dispatchOnBackgroundThread) override
	{
	}
	void PLUGIN_API queueClosed (Vst::DataExchangeUserContextID userContextID) override {}
	void PLUGIN_API onDataExchangeBlocksReceived (Vst::DataExchangeUserContextID userContextID,
	                                              uint32 numBlocks, Vst::DataExchangeBlock* block,
	                                              TBool onBackgroundThread) override;

	OBJ_METHODS (PlugController, Vst::EditController)
	DEFINE_INTERFACES
		DEF_INTERFACE (Vst::IDataExchangeReceiver)
	END_DEFINE_INTERFACES (EditController)
	DELEGATE_REFCOUNT (Vst::EditController)

private:
	Vst::DataExchangeReceiverHandler mDataExchange {this};

	uint32 mProcessState {0};
};

//------------------------------------------------------------------------
} // namespace TransportControl
} // namespace Steinberg
