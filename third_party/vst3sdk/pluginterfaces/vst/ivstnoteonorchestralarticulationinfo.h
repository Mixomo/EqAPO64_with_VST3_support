//------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : Interfaces
// Filename    : pluginterfaces/vst/ivstnoteonorchestralarticulationinfo.h
// Created by  : Steinberg, 07/2026
// Description : Support for MIDI-CI Profile for Note On Selection of Orchestral Articulation
//               (M2-123-UM)
//
//-----------------------------------------------------------------------------
// This file is part of a Steinberg SDK. It is subject to the license terms
// in the LICENSE file found in the top-level directory of this distribution
// and at www.steinberg.net/sdklicenses.
// No part of the SDK, including this file, may be copied, modified, propagated,
// or distributed except according to the terms contained in the LICENSE file.
//-----------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

//------------------------------------------------------------------------
#include "pluginterfaces/base/falignpush.h"
//------------------------------------------------------------------------

//------------------------------------------------------------------------
namespace Steinberg {
namespace Vst {
namespace NoteOnOrchestralArticulation {

//------------------------------------------------------------------------
struct Variations
{
	enum
	{
		kNumSubClasses = 16
	};
	uint8 variation[kNumSubClasses];
};

//------------------------------------------------------------------------
struct ClassificationVariations
{
	enum
	{
		kNumClassifications = 17,
	};
	Variations classification[kNumClassifications];
};

//------------------------------------------------------------------------
/** Extended host callback interface for an edit controller:
 * Vst::NoteOnOrchestralArticulation::IInfo
 * \ingroup vstIPlug vst381
 * - [plug imp]
 * - [extends IEditController]
 * - [released: 3.8.1]
 * - [optional]
 * 
 * This interface allows the host to get some information about the plug-in's noteOn
 *  Orchestral Articulation capabilities from the UI Thread
 */
//------------------------------------------------------------------------
class IInfo : public FUnknown
{
public:
	/** Used as index for the array classification of structure ClassificationVariations.
	 */
	enum Classification : uint32
	{
		kCoreSoundSustainAndStrikes = 0,
		kStaccatosAndShorts = 1,
		kSameNoteTrillsRepeats = 2,
		kIntervallicTrills = 3,
		kAdditionalColors = 4,
		kPitchAndDynamicGestures = 5,
		kScalesRunsAndArpeggios = 6,
		kEffectsAndNoises = 7,

		kReserved1 = 8,
		kReserved2 = 9,

		kCustomNoteOn1 = 10,
		kCustomNoteOn2 = 11,
		kCustomNoteOn3 = 12,
		kCustomNoteOn4 = 13,
		kCustomNoteOn5 = 14,
		kCustomNoteOn6 = 15,

		kReserved3 = 16
	};

	/** Returns variation information for orchestral classification types supported by the plug-in.
	 *
	 * The 2D Array is organized by classifications (see Classification) and subclass
	 * (according to the index used in MIDI2 Orchestral Profile).
	 * If info.classification[i].variation[j] is 0, subclass j has no variation (subclass not available).
	 * If all entries in info.classification[i] are zero, classification i is not available.
	 *
     * @param[in] busIndex - index of Input Event Bus
     * @param[in] channel - channel of the bus
	 * @param info 2D array receiving supported variation information.
	 * @return kResultTrue on success
	 *
	 * \note [UI-thread & Connected]
	 */
    virtual tresult PLUGIN_API getVariationsInfo (int32 busIndex, int16 channel, ClassificationVariations& info /*out*/) = 0;
                                                  
	//------------------------------------------------------------------------
	static const FUID iid;
};

DECLARE_CLASS_IID (IInfo, 0x7D34A9B4, 0x89CA45F4, 0x9284A9FD, 0x4DD9C906)

//------------------------------------------------------------------------
} // namespace NoteOnOrchestralArticulation
} // namespace Vst
} // namespace Steinberg

//------------------------------------------------------------------------
#include "pluginterfaces/base/falignpop.h"
//------------------------------------------------------------------------

