//------------------------------------------------------------------------
// Flags       : clang-format SMTGSequencer
// Project     : VST SDK
//
// Category    : Examples
// Filename    : public.sdk/samples/vst/transportcontrol/include/uitransportcontroller.h
// Created by  : Steinberg, 05/2026
// Description : Transport Control UI Controller
//
//-----------------------------------------------------------------------------
// This file is part of a Steinberg SDK. It is subject to the license terms
// in the LICENSE file found in the top-level directory of this distribution
// and at www.steinberg.net/sdklicenses.
// No part of the SDK, including this file, may be copied, modified, propagated,
// or distributed except according to the terms contained in the LICENSE file.
//-----------------------------------------------------------------------------

#pragma once

#include "vstgui/lib/iviewlistener.h"
#include "vstgui/uidescription/icontroller.h"
#include "pluginterfaces/vst/ivsttransportcontrol.h"

//------------------------------------------------------------------------
namespace Steinberg {
namespace Vst {

//------------------------------------------------------------------------
// UITransportController
//------------------------------------------------------------------------
class UITransportController : public VSTGUI::IController, public VSTGUI::ViewListenerAdapter
{
public:
	enum Tags
	{
		kLocateTag = 1000,
		kPlaybackStart,
		kPlaybackStop,
		kLocateAndPlaybackStart,
		kPlaybackStopAndLocate,

		kRecordOnPlaybackStart,
		kLocateAndRecordOnPlaybackStart,
		kRecordOff,
		kRecordOffPlaybackStop,
		kRecordOffPlaybackStopAndLocate,

		kSetCycleStart,
		kSetCycleEnd,
		kCycleOn,
		kCycleOff
	};

	UITransportController (IPtr<Vst::ITransportControl> transportControl)
	: mTransportControl (transportControl)
	{
	}
	~UITransportController () override
	{
		if (mTextEdit)
			viewWillDelete (mTextEdit);
	}

private:
	using CControl = VSTGUI::CControl;
	using CView = VSTGUI::CView;
	using CTextEdit = VSTGUI::CTextEdit;
	using UIAttributes = VSTGUI::UIAttributes;
	using IUIDescription = VSTGUI::IUIDescription;

	//--- from IControlListener ----------------------
	void valueChanged (CControl* pControl) override
	{
		if (!mTransportControl || !pControl)
			return;

		pControl->setValue (0.f);
		pControl->invalid ();

		Vst::TransportPosition pos;
		if (mTextEdit)
		{
			auto text = mTextEdit->getText ().getString ();
			auto posSamples = text.find ("samples");
			if (posSamples != std::string::npos)
			{
				// Extract the number before "samples"
				auto numberStr = text.substr (0, posSamples);
				int samples = std::stoi (numberStr);
				pos = Vst::TransportPosition::fromSamples (samples);
			}
			else // ppq
			{
				double ppq = std::stod (text);
				pos = Vst::TransportPosition::fromQuarterNotes (ppq);
			}
		}
		else
			pos = Vst::TransportPosition::fromSamples (48000 * 10.0);

		const int32 tag = pControl->getTag ();
		switch (tag)
		{
			case kLocateTag:
			{
				if (mTransportControl->isActionSupported (Vst::ITransportControl::Locate) ==
				    kResultTrue)
					mTransportControl->requestAction (Vst::ITransportControl::Locate, &pos);
			}
			break;
			case kPlaybackStart:
			{
				if (mTransportControl->isActionSupported (Vst::ITransportControl::PlaybackStart) ==
				    kResultTrue)
					mTransportControl->requestAction (Vst::ITransportControl::PlaybackStart);
			}
			break;
			case kPlaybackStop:
			{
				if (mTransportControl->isActionSupported (Vst::ITransportControl::PlaybackStop) ==
				    kResultTrue)
					mTransportControl->requestAction (Vst::ITransportControl::PlaybackStop);
			}
			break;
			case kLocateAndPlaybackStart:
			{
				if (mTransportControl->isActionSupported (
				        Vst::ITransportControl::LocateAndPlaybackStart) == kResultTrue)
					mTransportControl->requestAction (
					    Vst::ITransportControl::LocateAndPlaybackStart, &pos);
			}
			break;
			case kPlaybackStopAndLocate:
			{
				if (mTransportControl->isActionSupported (
				        Vst::ITransportControl::PlaybackStopAndLocate) == kResultTrue)
				{
					mTransportControl->requestAction (Vst::ITransportControl::PlaybackStopAndLocate,
					                                  &pos);
				}
			}
			break;
			case kRecordOnPlaybackStart:
			{
				if (mTransportControl->isActionSupported (
				        Vst::ITransportControl::RecordOnPlaybackStart) == kResultTrue)
					mTransportControl->requestAction (
					    Vst::ITransportControl::RecordOnPlaybackStart);
			}
			break;
			case kLocateAndRecordOnPlaybackStart:
			{
				if (mTransportControl->isActionSupported (
				        Vst::ITransportControl::LocateAndRecordOnPlaybackStart) == kResultTrue)
					mTransportControl->requestAction (
					    Vst::ITransportControl::LocateAndRecordOnPlaybackStart, &pos);
			}
			break;
			case kRecordOff:
			{
				if (mTransportControl->isActionSupported (Vst::ITransportControl::RecordOff) ==
				    kResultTrue)
					mTransportControl->requestAction (Vst::ITransportControl::RecordOff);
			}
			break;
			case kRecordOffPlaybackStop:
			{
				if (mTransportControl->isActionSupported (
				        Vst::ITransportControl::RecordOffPlaybackStop) == kResultTrue)
					mTransportControl->requestAction (
					    Vst::ITransportControl::RecordOffPlaybackStop);
			}
			break;
			case kRecordOffPlaybackStopAndLocate:
			{
				if (mTransportControl->isActionSupported (
				        Vst::ITransportControl::RecordOffPlaybackStopAndLocate) == kResultTrue)
					mTransportControl->requestAction (
					    Vst::ITransportControl::RecordOffPlaybackStopAndLocate, &pos);
			}
			break;
			case kSetCycleStart:
			{
				if (mTransportControl->isActionSupported (Vst::ITransportControl::SetCycleStart) ==
				    kResultTrue)
					mTransportControl->requestAction (Vst::ITransportControl::SetCycleStart, &pos);
			}
			break;
			case kSetCycleEnd:
			{
				if (mTransportControl->isActionSupported (Vst::ITransportControl::SetCycleEnd) ==
				    kResultTrue)
					mTransportControl->requestAction (Vst::ITransportControl::SetCycleEnd, &pos);
			}
			break;
			case kCycleOn:
			{
				if (mTransportControl->isActionSupported (Vst::ITransportControl::CycleOn) ==
				    kResultTrue)
					mTransportControl->requestAction (Vst::ITransportControl::CycleOn);
			}
			break;
			case kCycleOff:
			{
				if (mTransportControl->isActionSupported (Vst::ITransportControl::CycleOff) ==
				    kResultTrue)
					mTransportControl->requestAction (Vst::ITransportControl::CycleOff);
			}
			break;
		}
	}
	void controlBeginEdit (CControl* /*pControl*/) override {}
	void controlEndEdit (CControl* pControl) override {}

	//--- from IControlListener ----------------------
	//--- is called when a view is created -----
	CView* verifyView (CView* view, const UIAttributes& /*attributes*/,
	                   const IUIDescription* /*description*/) override
	{
		if (auto* te = dynamic_cast<CTextEdit*> (view))
		{
			// this allows us to keep a pointer of the text edit view
			mTextEdit = te;

			// add this as listener in order to get viewWillDelete and viewLostFocus calls
			mTextEdit->registerViewListener (this);

			// initialize it content
			mTextEdit->setText ("48000 samples");
		}
		return view;
	}
	//--- from IViewListenerAdapter ----------------------
	//--- is called when a view will be deleted: the editor is closed -----
	void viewWillDelete (CView* view) override
	{
		if (mTextEdit && (dynamic_cast<CTextEdit*> (view) == mTextEdit))
		{
			mTextEdit->unregisterViewListener (this);
			mTextEdit = nullptr;
		}
	}
	IPtr<Vst::ITransportControl> mTransportControl;
	CTextEdit* mTextEdit {nullptr};
};

//------------------------------------------------------------------------
} // Vst
} // Steinberg
