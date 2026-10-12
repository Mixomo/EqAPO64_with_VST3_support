//------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : Examples
// Filename    : public.sdk/samples/vst/transportcontrol/include/plugids.h
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

namespace Steinberg {
namespace TransportControl {

// HERE are defined the parameter Ids which are exported to the host
enum TransportControlParams : Vst::ParamID
{
	kBypassId = 100,
	kPlaybackId = 101,
	kRecordingId = 102,
	kCycleActiveId = 103

};


// HERE you have to define new unique class ids: for processor and for controller
// you can use GUID creator tools like https://www.guidgenerator.com/
static const FUID MyProcessorUID (0x5E12B8CB, 0x1E924F8E, 0x8551BC8C, 0x1E312392);
static const FUID MyControllerUID (0xE7569C15, 0x3B6948E5, 0x86CA2A1E, 0x19D08E23);

//------------------------------------------------------------------------
} // namespace TransportControl
} // namespace Steinberg
