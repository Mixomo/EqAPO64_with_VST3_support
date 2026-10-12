//-----------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : Helpers
// Filename    : public.sdk/source/vst/hosting/test/miditovsttest.cpp
// Created by  : Steinberg, 02/2026
// Description : Test MIDI to VST conversions
// Flags       : clang-format SMTGSequencer
//
//-----------------------------------------------------------------------------
// This file is part of a Steinberg SDK. It is subject to the license terms
// in the LICENSE file found in the top-level directory of this distribution
// and at www.steinberg.net/sdklicenses.
// No part of the SDK, including this file, may be copied, modified, propagated,
// or distributed except according to the terms contained in the LICENSE file.
//-----------------------------------------------------------------------------

#include "public.sdk/source/main/moduleinit.h"
#include "public.sdk/source/vst/utility/testing.h"

#include "public.sdk/samples/vst-hosting/audiohost/source/media/miditovst.h"

//------------------------------------------------------------------------
namespace Steinberg {
namespace Vst {
namespace {

ParamID toParamID (int32, MidiData controllerNumber)
{
	if (controllerNumber < kCountCtrlNumber)
		return static_cast<ParamID> (controllerNumber);
	return kNoParamId;
};

//------------------------------------------------------------------------
ModuleInitializer MIDI2VSTTests ([] () {
	constexpr auto TestSuiteName = "MIDI2VST";
	registerTest (TestSuiteName, STR ("midiToEvent noteOn"), [] (ITestResult* testResult) {
		{
			auto event = midiToEvent (kNoteOn, 0, 60, 64);
			EXPECT_TRUE (event);
			if (event)
			{
				EXPECT_EQ (event->type, Event::kNoteOnEvent);
				EXPECT_EQ (event->noteOn.channel, 0);
				EXPECT_EQ (event->noteOn.pitch, 60);
				EXPECT_EQ (event->noteOn.velocity, 0.5f);
			}
		}
		{
			auto event = midiToEvent (kNoteOn, 6, 94, 127);
			EXPECT_TRUE (event);
			if (event)
			{
				EXPECT_EQ (event->type, Event::kNoteOnEvent);
				EXPECT_EQ (event->noteOn.channel, 6);
				EXPECT_EQ (event->noteOn.pitch, 94);
				EXPECT_EQ (event->noteOn.velocity, 1.f);
			}
		}
		return true;
	});
	registerTest (TestSuiteName, STR ("midiToEvent noteOff"), [] (ITestResult* testResult) {
		{
			auto event = midiToEvent (kNoteOff, 0, 60, 64);
			EXPECT_TRUE (event);
			if (event)
			{
				EXPECT_EQ (event->type, Event::kNoteOffEvent);
				EXPECT_EQ (event->noteOff.channel, 0);
				EXPECT_EQ (event->noteOff.pitch, 60);
				EXPECT_EQ (event->noteOff.velocity, 0.5f);
			}
		}
		{
			auto event = midiToEvent (kNoteOff, 6, 94, 127);
			EXPECT_TRUE (event);
			if (event)
			{
				EXPECT_EQ (event->type, Event::kNoteOffEvent);
				EXPECT_EQ (event->noteOff.channel, 6);
				EXPECT_EQ (event->noteOff.pitch, 94);
				EXPECT_EQ (event->noteOff.velocity, 1.f);
			}
		}
		return true;
	});
	registerTest (TestSuiteName, STR ("midiToEvent polyPressure"), [] (ITestResult* testResult) {
		{
			auto event = midiToEvent (kPolyPressure, 0, 60, 64);
			EXPECT_TRUE (event);
			if (event)
			{
				EXPECT_EQ (event->type, Event::kPolyPressureEvent);
				EXPECT_EQ (event->polyPressure.channel, 0);
				EXPECT_EQ (event->polyPressure.pitch, 60);
				EXPECT_EQ (event->polyPressure.pressure, 0.5f);
			}
		}
		{
			auto event = midiToEvent (kPolyPressure, 6, 94, 127);
			EXPECT_TRUE (event);
			if (event)
			{
				EXPECT_EQ (event->type, Event::kPolyPressureEvent);
				EXPECT_EQ (event->polyPressure.channel, 6);
				EXPECT_EQ (event->polyPressure.pitch, 94);
				EXPECT_EQ (event->polyPressure.pressure, 1.f);
			}
		}
		return true;
	});
	registerTest (TestSuiteName, STR ("midiToEvent unsupported"), [] (ITestResult* testResult) {
		EXPECT_FALSE (midiToEvent (kController, 3, 44, 77));
		EXPECT_FALSE (midiToEvent (kProgramChangeStatus, 9, 1, 2));
		EXPECT_FALSE (midiToEvent (kAfterTouchStatus, 11, 99, 0));
		EXPECT_FALSE (midiToEvent (kPitchBendStatus, 2, 12, 34));
		return true;
	});

	registerTest (TestSuiteName, STR ("midiToParameter controller"), [] (ITestResult* testResult) {
		{
			auto paramChange = midiToParameter (kController, 7, 88, 64, toParamID);
			EXPECT_TRUE (paramChange);
			if (paramChange)
			{
				EXPECT_EQ (paramChange->first, 88);
				EXPECT_EQ (paramChange->second, 0.5);
			}
		}
		{
			auto paramChange = midiToParameter (kController, 6, 94, 127, toParamID);
			EXPECT_TRUE (paramChange);
			if (paramChange)
			{
				EXPECT_EQ (paramChange->first, 94);
				EXPECT_EQ (paramChange->second, 1.);
			}
		}
		return true;
	});
	registerTest (TestSuiteName, STR ("midiToParameter pitchBend"), [] (ITestResult* testResult) {
		{
			auto paramChange = midiToParameter (kPitchBendStatus, 1, 0x00, 0x00, toParamID);
			EXPECT_TRUE (paramChange);
			if (paramChange)
			{
				EXPECT_EQ (paramChange->first, kPitchBend);
				EXPECT_EQ (paramChange->second, 0.0);
			}
		}
		{
			auto paramChange = midiToParameter (kPitchBendStatus, 1, 0x00, 0x10, toParamID);
			EXPECT_TRUE (paramChange);
			if (paramChange)
			{
				EXPECT_EQ (paramChange->first, kPitchBend);
				EXPECT_EQ (paramChange->second, 0.125);
			}
		}
		{
			auto paramChange = midiToParameter (kPitchBendStatus, 1, 0x00, 0x20, toParamID);
			EXPECT_TRUE (paramChange);
			if (paramChange)
			{
				EXPECT_EQ (paramChange->first, kPitchBend);
				EXPECT_EQ (paramChange->second, 0.25);
			}
		}
		{
			auto paramChange = midiToParameter (kPitchBendStatus, 1, 0x00, 0x30, toParamID);
			EXPECT_TRUE (paramChange);
			if (paramChange)
			{
				EXPECT_EQ (paramChange->first, kPitchBend);
				EXPECT_EQ (paramChange->second, 0.375);
			}
		}
		{
			auto paramChange = midiToParameter (kPitchBendStatus, 7, 0x00, 0x40, toParamID);
			EXPECT_TRUE (paramChange);
			if (paramChange)
			{
				EXPECT_EQ (paramChange->first, kPitchBend);
				EXPECT_EQ (paramChange->second, 0.5);
			}
		}
		{
			auto paramChange = midiToParameter (kPitchBendStatus, 6, 0x7F, 0x5F, toParamID);
			EXPECT_TRUE (paramChange);
			if (paramChange)
			{
				EXPECT_EQ (paramChange->first, kPitchBend);
				EXPECT_TRUE (fabs (paramChange->second - 0.75) < 1e-3);
			}
		}
		{
			auto paramChange = midiToParameter (kPitchBendStatus, 6, 0x7F, 0x7F, toParamID);
			EXPECT_TRUE (paramChange);
			if (paramChange)
			{
				EXPECT_EQ (paramChange->first, kPitchBend);
				EXPECT_EQ (paramChange->second, 1.);
			}
		}
		return true;
	});
	registerTest (
	    TestSuiteName, STR ("midiToParameter channelPressure"), [] (ITestResult* testResult) {
		    std::function<ParamID (int32, MidiData)> toParamID = [] (int32 channel,
		                                                             MidiData controllerNumber) {
			    return static_cast<ParamID> (controllerNumber);
		    };
		    {
			    auto paramChange = midiToParameter (kAfterTouchStatus, 7, 64, 0, toParamID);
			    EXPECT_TRUE (paramChange);
			    if (paramChange)
			    {
				    EXPECT_EQ (paramChange->first, kAfterTouch);
				    EXPECT_EQ (paramChange->second, 0.5);
			    }
		    }
		    {
			    auto paramChange = midiToParameter (kAfterTouchStatus, 6, 127, 0, toParamID);
			    EXPECT_TRUE (paramChange);
			    if (paramChange)
			    {
				    EXPECT_EQ (paramChange->first, kAfterTouch);
				    EXPECT_EQ (paramChange->second, 1.);
			    }
		    }
		    return true;
	    });
	registerTest (TestSuiteName, STR ("midiToParameter unsupported"), [] (ITestResult* testResult) {
		EXPECT_FALSE (midiToParameter (kNoteOn, 3, 44, 77, toParamID));
		EXPECT_FALSE (midiToParameter (kNoteOff, 9, 1, 2, toParamID));
		EXPECT_FALSE (midiToParameter (kPolyPressure, 11, 99, 0, toParamID));
		EXPECT_FALSE (midiToParameter (kProgramChangeStatus, 2, 12, 34, toParamID));
		EXPECT_FALSE (midiToParameter (kPitchBendStatus, 2, 12, 34, {}));
		EXPECT_FALSE (midiToParameter (130, 2, 12, 34, {}));
		return true;
	});
});

//------------------------------------------------------------------------
} // anonymous
} // Vst
} // Steinberg
