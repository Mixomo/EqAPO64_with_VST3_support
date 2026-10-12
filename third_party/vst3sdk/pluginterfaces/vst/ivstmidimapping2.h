//------------------------------------------------------------------------
// Flags       : clang-format SMTGSequencer
// Project     : VST SDK
//
// Category    : Interfaces
// Filename    : pluginterfaces/vst/ivstmidimapping2.h
// Created by  : Steinberg, 10/2025
// Description : MIDI controller mapping (includes MIDI 2.0)
//
//-----------------------------------------------------------------------------
// This file is part of a Steinberg SDK. It is subject to the license terms
// in the LICENSE file found in the top-level directory of this distribution
// and at www.steinberg.net/sdklicenses.
// No part of the SDK, including this file, may be copied, modified, propagated,
// or distributed except according to the terms contained in the LICENSE file.
//-----------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/vsttypes.h"

//------------------------------------------------------------------------
#include "pluginterfaces/base/falignpush.h"
//------------------------------------------------------------------------

//------------------------------------------------------------------------
namespace Steinberg {
namespace Vst {

using MidiGroup = uint8;
using MidiChannel = uint8;
using BusIndex = int32;

//------------------------------------------------------------------------
/** Midi2Controller
 *
 *	describes a MIDI 2.0 Registered or Assignable Controller
 *
 */
struct Midi2Controller
{
	/** Bits 0-6 of byte1 specify the bank. Bit 7 (MSB) indicates the controller type:
	 *  set = registered controller, clear = assignable controller. */
	uint8 byte1;
	/** Bits 0-6 of byte2 specify the controller index. */
	uint8 byte2;

	// -- Helper functions:

	/** Returns true if the MIDI2 controller struct is a registered controller and false if it is an
	 *  assignable controller. */
	static bool isRegisteredController (const Midi2Controller& mc) { return mc.byte1 & 0x80; }
	/** Returns the bank number of the MIDI2 controller struct. */
	static uint8 bank (const Midi2Controller& mc) { return mc.byte1 & 0x7F; }
	/** Returns the index number of the MIDI2 controller struct. */
	static uint8 index (const Midi2Controller& mc) { return mc.byte2 & 0x7F; }

	/** Creates a new MIDI2 controller struct. */
	static Midi2Controller create (bool registered, uint8 bank, uint8 index)
	{
		Midi2Controller m2c {};
		setIsRegisteredController (m2c, registered);
		setBank (m2c, bank);
		setIndex (m2c, index);
		return m2c;
	}
	/** Sets the flag of the MIDI2 controller struct if it describes a registered or assignable
	 *	controller. */
	static void setIsRegisteredController (Midi2Controller& mc, bool registered)
	{
		mc.byte1 = (mc.byte1 & 0x7F) | (registered ? 0x80 : 0x00);
	}
	/** Set the bank number of the MIDI2 controller struct. */
	static void setBank (Midi2Controller& mc, uint8 bank) { mc.byte1 = (mc.byte1 & 0x80) | bank; }
	/** Set the index number of the MIDI2 controller struct. */
	static void setIndex (Midi2Controller& mc, uint8 index) { mc.byte2 = index & 0x7F; }
};

//------------------------------------------------------------------------
struct Midi2ControllerParamIDAssignment
{
	ParamID pId;
	BusIndex busIndex;
	MidiChannel channel;
	Midi2Controller controller;
};

//------------------------------------------------------------------------
struct Midi2ControllerParamIDAssignmentList
{
	uint32 count;
	Midi2ControllerParamIDAssignment* map;
};

//------------------------------------------------------------------------
struct Midi1ControllerParamIDAssignment
{
	ParamID pId;
	BusIndex busIndex;
	MidiChannel channel;
	CtrlNumber controller;
};

//------------------------------------------------------------------------
struct Midi1ControllerParamIDAssignmentList
{
	uint32 count;
	Midi1ControllerParamIDAssignment* map;
};

//------------------------------------------------------------------------
/** MIDI Mapping interface: Vst::IMidiMapping2
\ingroup vstIPlug vst380
- [plug imp]
- [extends IEditController]
- [released: 3.8.0]
- [optional]
- [replaces Vst::IMidiMapping]

This interface replaces Vst::IMidiMapping to support the extended MIDI controllers in MIDI 2.0.

A MIDI 2.0 capable host first queries for the Vst::IMidiMapping2 interface and uses the old
Vst::IMidiMapping interface as a fallback.

A plug-in can use the Vst::IPlugInterfaceSupport to check if the host supports Vst::IMidiMapping2.
*/
class IMidiMapping2 : public FUnknown
{
public:
	/** Gets the number of MIDI 2.0 controller to parameter assignments
	 *
	 *	@param direction	input/output direction
	 *	@return 			number of MIDI 2.0 controller to parameter assignments
	 * \note [UI-thread & Connected] */
	virtual uint32 PLUGIN_API getNumMidi2ControllerAssignments (BusDirections direction) = 0;

	/** Gets MIDI 2.0 controller parameter assignments
	 *
	 *	the list is preallocated by the host and must be filled by the plug-in
	 *
	 *	@param direction	input/output direction
	 *	@param list			list of assignments
	 *	@return 			kResultTrue on success
	 * \note [UI-thread & Connected] */
	virtual tresult PLUGIN_API getMidi2ControllerAssignments (
	    BusDirections direction, const Midi2ControllerParamIDAssignmentList& list) = 0;

	/** Gets the number of MIDI 1.0 controller to parameter assignments
	 *
	 *	@param direction	input/output direction
	 *	@return 			number of MIDI 1.0 controller to parameter assignments
	 * \note [UI-thread & Connected] */
	virtual uint32 PLUGIN_API getNumMidi1ControllerAssignments (BusDirections direction) = 0;

	/** Gets MIDI 1.0 controller parameter assignments
	 *
	 *	the list is preallocated by the host and must be filled by the plug-in
	 *
	 *	@param direction	input/output direction
	 *	@param list			list of assignments
	 *	@return 			kResultTrue on success
	 * \note [UI-thread & Connected] */
	virtual tresult PLUGIN_API getMidi1ControllerAssignments (
	    BusDirections direction, const Midi1ControllerParamIDAssignmentList& list) = 0;

//------------------------------------------------------------------------
	static const FUID iid;
};

DECLARE_CLASS_IID (IMidiMapping2, 0x6DE14B88, 0x03F94F09, 0xA2552F0F, 0x9326593E)

//------------------------------------------------------------------------
/** MIDI Learn interface: Vst::IMidiLearn2
\ingroup vstIPlug vst380
- [plug imp]
- [extends IEditController]
- [released: 3.8.0]
- [optional]
- [replaces Vst::IMidiLearn]

If this interface is implemented by the edit controller, the host will call this method whenever
there is live MIDI-CC input for the plug-in. This way, the plug-in can change its MIDI-CC parameter
mapping and notify the host using IComponentHandler::restartComponent with the
kMidiCCAssignmentChanged flag.
Use this if you want to implement custom MIDI-Learn functionality in your plug-in.
*/
class IMidiLearn2 : public FUnknown
{
public:
	/** Called on live input MIDI 2.0-CC change associated to a given bus index and MIDI channel
	 * \note [UI-thread & (Initialized | Connected)] */
	virtual tresult PLUGIN_API onLiveMidi2ControllerInput (BusIndex index, MidiChannel channel,
	                                                       Midi2Controller midiCC) = 0;

	/** Called on live input MIDI 1.0-CC change associated to a given bus index and MIDI channel
	 * \note [UI-thread & (Initialized | Connected)] */
	virtual tresult PLUGIN_API onLiveMidi1ControllerInput (BusIndex index, MidiChannel channel,
	                                                       CtrlNumber midiCC) = 0;

//------------------------------------------------------------------------
	static const FUID iid;
};

DECLARE_CLASS_IID (IMidiLearn2, 0xF07E498A, 0x78864327, 0x8B431CED, 0xA3C553FC)

//------------------------------------------------------------------------
} // namespace Vst
} // namespace Steinberg

//------------------------------------------------------------------------
#include "pluginterfaces/base/falignpop.h"
//------------------------------------------------------------------------
