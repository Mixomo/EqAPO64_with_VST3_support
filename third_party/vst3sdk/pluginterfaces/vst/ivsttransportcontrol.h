//------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : Interfaces
// Filename    : pluginterfaces/vst/ivsttransportcontrol.h
// Created by  : Steinberg, 03/2026
// Description :
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

//------------------------------------------------------------------------
/** Transport Position Type */
enum TransportPositionType : int32
{
	Samples, ///< Absolute position in samples
	QuarterNotes ///< Musical position in quarter notes (PPQ)
};

//------------------------------------------------------------------------
/** TransportPosition
 *
 * A tagged union representing a transport position in either:
 *  - absolute samples (TSamples), or
 *  - musical quarter notes (TQuarterNotes, a.k.a. PPQ).
 *
 * Hosts may choose which representation they honor.
 * Plug-ins should use ::Type to indicate which field is valid.
 *
 * \note TransportPosition is only meaningful for actions requiring a position
 *       (Locate, SetCycleStart, SetCycleEnd, etc).
 */
struct TransportPosition
{
	TransportPositionType type {TransportPositionType::Samples};

	union
	{
		TSamples samples; ///< Absolute sample position
		TQuarterNotes quarterNotes; ///< PPQ position (musical time)
	};

	//--------------------------------------------------------------------
		/** Create from absolute samples. */
	static TransportPosition fromSamples (TSamples s)
	{
		TransportPosition p;
		p.type = TransportPositionType::Samples;
		p.samples = s;
		return p;
	}

	/** Create from quarter notes (PPQ). */
	static TransportPosition fromQuarterNotes (TQuarterNotes q)
	{
		TransportPosition p;
		p.type = TransportPositionType::QuarterNotes;
		p.quarterNotes = q;
		return p;
	}
};

//------------------------------------------------------------------------
/** Vst::ITransportControl Interface
\ingroup vstIHost vst381
- [host imp]
- [extends IComponentHandler]
- [released: 3.8.1]
- [optional]

This interface allows a plug-in to request transport-related actions from the host.
Typical use cases include:
- Jumping (locating) to a position
- Starting or stopping playback or recording
- Adjusting loop/cycle regions
- Enabling or disabling cycle playback

All calls must be made from the **UI thread**.
Hosts may accept or deny requests depending on their internal policies or the
current editing mode (e.g., offline rendering, write-protected state, etc.).
*/
class ITransportControl : public FUnknown
{
public:
	//--------------------------------------------------------------------
	/** Transport action requested by the plug-in. */
	enum Action : int32
	{
		/** Locate transport to a new position.
		 *  Requires a valid TransportPosition. */
		Locate = 0,

		/** Playback control */
		PlaybackStart,			///< Start playback at current position
		PlaybackStop,			///< Stop playback, keep current position
		LocateAndPlaybackStart,	///< Locate and start playback (position required)
		PlaybackStopAndLocate,	///< Stop and locate (position required)

		/** Recording control */
		RecordOnPlaybackStart,			///< Start recording and playback at current position
		LocateAndRecordOnPlaybackStart,	///< Locate and start recording and playback (position required)
		RecordOff,						///< Stop recording and continue playback
		RecordOffPlaybackStop,			///< Stop recording and playback, keep current position
		RecordOffPlaybackStopAndLocate,	///< Stop recording and playback and locate (position required)

		/** Cycle / Loop control */
		SetCycleStart,	///< Set start of cycle/loop region (position required)
		SetCycleEnd,	///< Set end of cycle/loop region (position required)
		CycleOn,		///< Enable looping/cycle mode
		CycleOff		///< Disable looping/cycle mode
	};

	//--------------------------------------------------------------------
	/** Request a transport action from the host.
	 *
	 * The host may accept the request or refuse it based on its current state
	 * (editing mode, user preferences, restricted transport control policies).
	 *
	 * @param action   The action to perform.
	 * @param position Optional pointer to a TransportPosition.
	 *                 Required for locate or cycle-related actions.
	 *
	 * @return kResultOk          The request is accepted (not necessarily completed synchronously).
	 *         kNotImplemented    Host does not implement this function.
	 *         kInvalidArgument   Position missing or invalid for the supplied action.
	 *         kResultFalse       Host refuses the action due to state/policy constraints.
	 *
	 * \note Must be called from the UI/main thread.
	 * \note [UI-thread & (Initialized | Connected)] */
	virtual tresult PLUGIN_API requestAction (int32 action /*in*/,
	                                          TransportPosition* position = nullptr /*in*/) = 0;

	//--------------------------------------------------------------------
	/** Query whether the host supports the given action.
	 *
	 * This is useful for updating plug-in UI elements (enabling or disabling buttons).
	 *
	 * @param action The action to test.
	 *
	 * @return kResultOk        The action is supported.
	 *         kResultFalse     The action is not supported.
	 *         kNotImplemented  Host does not implement this function.
	 *         kInvalidArgument Unknown action.
	 *
	 * \note Must be called from the UI/main thread.
	 * \note [UI-thread & (Initialized | Connected)] */
	virtual tresult PLUGIN_API isActionSupported (int32 action /*in*/) = 0;

	//--------------------------------------------------------------------
	static const FUID iid;
};

//------------------------------------------------------------------------
DECLARE_CLASS_IID (ITransportControl, 0xD752F52D, 0x004348A0, 0xB431B045, 0xF0DCA49C)

//------------------------------------------------------------------------
} // namespace Vst
} // namespace Steinberg

//------------------------------------------------------------------------
#include "pluginterfaces/base/falignpop.h"
//------------------------------------------------------------------------
