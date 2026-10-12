//-----------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : Helpers
// Filename    : public.sdk/source/vst/utility/test/midiconverttest.cpp
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
#include "public.sdk/source/vst/utility/midiconvert.h"
#include "public.sdk/source/vst/utility/testing.h"
#include "pluginterfaces/base/fstrdefs.h"

//------------------------------------------------------------------------
namespace Steinberg {
namespace Vst {
namespace {

//------------------------------------------------------------------------
ModuleInitializer MIDIConvertTests ([] () {
	constexpr auto TestSuiteName = "MIDIConvert";
	registerTest (TestSuiteName, STR ("midi7BitFromNormalized"), [] (ITestResult* testResult) {
		EXPECT_EQ (midi7BitFromNormalized<float> (0.f), 0);
		EXPECT_EQ (midi7BitFromNormalized<float> (0.25f), 32);
		EXPECT_EQ (midi7BitFromNormalized<float> (0.5f), 64);
		EXPECT_EQ (midi7BitFromNormalized<float> (1.f), 127);

		EXPECT_EQ (midi7BitFromNormalized<double> (0.), 0);
		EXPECT_EQ (midi7BitFromNormalized<double> (0.25), 32);
		EXPECT_EQ (midi7BitFromNormalized<double> (0.5), 64);
		EXPECT_EQ (midi7BitFromNormalized<double> (1.), 127);

		return true;
	});
	registerTest (TestSuiteName, STR ("midi14BitFromNormalized"), [] (ITestResult* testResult) {
		EXPECT_EQ (midi14BitFromNormalized<float> (0.f), 0);
		EXPECT_EQ (midi14BitFromNormalized<float> (0.25f), 0x1000);
		EXPECT_EQ (midi14BitFromNormalized<float> (0.5f), 0x2000);
		EXPECT_EQ (midi14BitFromNormalized<float> (1.f), 0x3FFF);

		EXPECT_EQ (midi14BitFromNormalized<double> (0.), 0);
		EXPECT_EQ (midi14BitFromNormalized<double> (0.25), 0x1000);
		EXPECT_EQ (midi14BitFromNormalized<double> (0.5), 0x2000);
		EXPECT_EQ (midi14BitFromNormalized<double> (1.), 0x3FFF);

		return true;
	});
	registerTest (TestSuiteName, STR ("midiIntegralFromNormalized uint16_t"),
	              [] (ITestResult* testResult) {
		              EXPECT_EQ ((midiIntegralFromNormalized<uint16_t, float> (0.f)), 0);
		              EXPECT_EQ ((midiIntegralFromNormalized<uint16_t, float> (0.25f)), 0x4000);
		              EXPECT_EQ ((midiIntegralFromNormalized<uint16_t, float> (0.5f)), 0x8000);
		              EXPECT_EQ ((midiIntegralFromNormalized<uint16_t, float> (1.f)), 0xFFFF);

		              EXPECT_EQ ((midiIntegralFromNormalized<uint16_t, double> (0.)), 0);
		              EXPECT_EQ ((midiIntegralFromNormalized<uint16_t, double> (0.25)), 0x4000);
		              EXPECT_EQ ((midiIntegralFromNormalized<uint16_t, double> (0.5)), 0x8000);
		              EXPECT_EQ ((midiIntegralFromNormalized<uint16_t, double> (1.)), 0xFFFF);

		              return true;
	              });
	registerTest (TestSuiteName, STR ("midiIntegralFromNormalized uint32_t"),
	              [] (ITestResult* testResult) {
		              EXPECT_EQ ((midiIntegralFromNormalized<uint32_t, float> (0.f)), 0);
		              EXPECT_EQ ((midiIntegralFromNormalized<uint32_t, float> (0.25f)), 0x40000000);
		              EXPECT_EQ ((midiIntegralFromNormalized<uint32_t, float> (0.5f)), 0x80000000);
		              EXPECT_EQ ((midiIntegralFromNormalized<uint32_t, float> (1.f)), 0xFFFFFFFF);

		              EXPECT_EQ ((midiIntegralFromNormalized<uint32_t, double> (0.)), 0);
		              EXPECT_EQ ((midiIntegralFromNormalized<uint32_t, double> (0.25)), 0x40000000);
		              EXPECT_EQ ((midiIntegralFromNormalized<uint32_t, double> (0.5)), 0x80000000);
		              EXPECT_EQ ((midiIntegralFromNormalized<uint32_t, double> (1.)), 0xFFFFFFFF);

		              return true;
	              });
	registerTest (TestSuiteName, STR ("midi7BitToNormalized"), [] (ITestResult* testResult) {
		EXPECT_EQ (midi7BitToNormalized<float> (0), 0.f);
		EXPECT_EQ (midi7BitToNormalized<float> (32), 0.25f);
		EXPECT_EQ (midi7BitToNormalized<float> (64), 0.5f);
		EXPECT_EQ (midi7BitToNormalized<float> (127), 1.f);

		EXPECT_EQ (midi7BitToNormalized<double> (0), 0.);
		EXPECT_EQ (midi7BitToNormalized<double> (32), 0.25);
		EXPECT_EQ (midi7BitToNormalized<double> (64), 0.5);
		EXPECT_EQ (midi7BitToNormalized<double> (127), 1.);

		return true;
	});
	registerTest (TestSuiteName, STR ("midi14BitToNormalized"), [] (ITestResult* testResult) {
		EXPECT_EQ (midi14BitToNormalized<float> (0), 0.f);
		EXPECT_EQ (midi14BitToNormalized<float> (0x1000), 0.25f);
		EXPECT_EQ (midi14BitToNormalized<float> (0x2000), 0.5f);
		EXPECT_EQ (midi14BitToNormalized<float> (0x3FFF), 1.f);

		EXPECT_EQ (midi14BitToNormalized<double> (0), 0.);
		EXPECT_EQ (midi14BitToNormalized<double> (0x1000), 0.25);
		EXPECT_EQ (midi14BitToNormalized<double> (0x2000), 0.5);
		EXPECT_EQ (midi14BitToNormalized<double> (0x3FFF), 1.);

		return true;
	});
	registerTest (TestSuiteName, STR ("midiIntegralToNormalized uint16_t"),
	              [] (ITestResult* testResult) {
		              EXPECT_EQ ((midiIntegralToNormalized<uint16_t, float> (0)), 0.f);
		              EXPECT_EQ ((midiIntegralToNormalized<uint16_t, float> (0x4000)), 0.25f);
		              EXPECT_EQ ((midiIntegralToNormalized<uint16_t, float> (0x8000)), 0.5f);
		              EXPECT_EQ ((midiIntegralToNormalized<uint16_t, float> (0xFFFF)), 1.f);

		              EXPECT_EQ ((midiIntegralToNormalized<uint16_t, double> (0)), 0.);
		              EXPECT_EQ ((midiIntegralToNormalized<uint16_t, double> (0x4000)), 0.25);
		              EXPECT_EQ ((midiIntegralToNormalized<uint16_t, double> (0x8000)), 0.5);
		              EXPECT_EQ ((midiIntegralToNormalized<uint16_t, double> (0xFFFF)), 1.);

		              return true;
	              });
	registerTest (
	    TestSuiteName, STR ("midiIntegralToNormalized uint32_t"), [] (ITestResult* testResult) {
		    EXPECT_EQ ((midiIntegralToNormalized<uint32_t, float> (0)), 0.f);
		    EXPECT_EQ ((midiIntegralToNormalized<uint32_t, float> (0x40000000)), 0.25f);
		    EXPECT_EQ ((midiIntegralToNormalized<uint32_t, float> (0x80000000)), 0.5f);
		    EXPECT_EQ ((midiIntegralToNormalized<uint32_t, float> (0xFFFFFFFF)), 1.f);

		    EXPECT_EQ ((midiIntegralToNormalized<uint32_t, double> (0)), 0.);
		    EXPECT_EQ ((midiIntegralToNormalized<uint32_t, double> (0x40000000)), 0.25);
		    EXPECT_EQ ((midiIntegralToNormalized<uint32_t, double> (0x80000000)), 0.5);
		    EXPECT_TRUE (fabs (midiIntegralToNormalized<uint32_t, double> (0xBFFFFFFF) - 0.75) <
		                 1e-8);
		    EXPECT_EQ ((midiIntegralToNormalized<uint32_t, double> (0xFFFFFFFF)), 1.);

		    return true;
	    });
});

//------------------------------------------------------------------------
} // anonymous
} // Vst
} // Steinberg
