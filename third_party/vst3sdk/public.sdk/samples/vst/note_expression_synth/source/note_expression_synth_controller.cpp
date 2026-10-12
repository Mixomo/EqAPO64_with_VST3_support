//-----------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : Examples
// Filename    : public.sdk/samples/vst/note_expression_synth/source/note_expression_synth_controller.cpp
// Created by  : Steinberg, 02/2010
// Description : 
//
//-----------------------------------------------------------------------------
// This file is part of a Steinberg SDK. It is subject to the license terms
// in the LICENSE file found in the top-level directory of this distribution
// and at www.steinberg.net/sdklicenses. 
// No part of the SDK, including this file, may be copied, modified, propagated,
// or distributed except according to the terms contained in the LICENSE file.
//-----------------------------------------------------------------------------

#include "note_expression_synth_controller.h"
#include "note_expression_synth_voice.h" // only needed for setComponentState
#include "base/source/fstring.h"
#include "pluginterfaces/base/futils.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

namespace Steinberg {
namespace Vst {
namespace NoteExpressionSynth {

FUID Controller::cid (0x2AC0A888, 0x9406497F, 0xBBA6EABF, 0xC78D1372);

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
class PanNoteExpressionType : public RangeNoteExpressionType
{
public:
	PanNoteExpressionType ()
	: RangeNoteExpressionType (
	      kPanTypeID, String ("Pan"), String ("Pan"), nullptr, kNoParentUnitId, 0, -100, 100,
	      NoteExpressionTypeInfo::kIsBipolar | NoteExpressionTypeInfo::kIsAbsolute)
	{
	}

	tresult getStringByValue (NoteExpressionValue valueNormalized /*in*/,
	                          String128 string /*out*/) SMTG_OVERRIDE
	{
		if (valueNormalized == 0.5)
			UString128 ("C").copyTo (string, 128);
		else if (valueNormalized == 0)
			UString128 ("L").copyTo (string, 128);
		else if (valueNormalized == 1)
			UString128 ("R").copyTo (string, 128);
		else
			RangeNoteExpressionType::getStringByValue (valueNormalized, string);
		return kResultTrue;
	}

	tresult getValueByString (const TChar* string /*in*/,
	                          NoteExpressionValue& valueNormalized /*out*/) SMTG_OVERRIDE
	{
		String str (string);
		if (str == "C")
		{
			valueNormalized = 0.5;
			return kResultTrue;
		}
		if (str == "L")
		{
			valueNormalized = 0.;
			return kResultTrue;
		}
		if (str == "R")
		{
			valueNormalized = 1.;
			return kResultTrue;
		}
		return RangeNoteExpressionType::getValueByString (string, valueNormalized);
	}
	OBJ_METHODS (PanNoteExpressionType, RangeNoteExpressionType)
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
class ReleaseTimeModNoteExpressionType : public NoteExpressionType
{
public:
	ReleaseTimeModNoteExpressionType ()
	: NoteExpressionType (Controller::kReleaseTimeModTypeID, String ("Release Time"),
	                      String ("RelTime"), String ("%"), kNoParentUnitId, 0.5, 0., 1., 0,
	                      NoteExpressionTypeInfo::kIsBipolar | NoteExpressionTypeInfo::kIsOneShot)
	{
	}

	tresult getStringByValue (NoteExpressionValue valueNormalized /*in*/,
	                          String128 string /*out*/) SMTG_OVERRIDE
	{
		UString128 wrapper;
		double timeFactor = pow (100., 2 * (valueNormalized - 0.5));
		wrapper.printFloat (timeFactor, timeFactor > 10 ? 1 : 2);
		wrapper.copyTo (string, 128);
		return kResultTrue;
	}

	tresult getValueByString (const TChar* string /*in*/,
	                          NoteExpressionValue& valueNormalized /*out*/) SMTG_OVERRIDE
	{
		String wrapper ((TChar*)string);
		ParamValue tmp {};
		if (wrapper.scanFloat (tmp))
		{
			valueNormalized = Bound (0.0, 1.0, log10 (tmp) / 4. + 0.5);
			return kResultTrue;
		}
		return kResultFalse;
	}
	OBJ_METHODS (ReleaseTimeModNoteExpressionType, NoteExpressionType)
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
tresult PLUGIN_API Controller::initialize (FUnknown* context)
{
	tresult result = EditController::initialize (context);
	if (result != kResultTrue)
		return result;

	// Init parameters
	Parameter* param = nullptr;

	param = new RangeParameter (USTRING ("Master Volume"), kParamMasterVolume, USTRING ("%"), 0,
	                            100, 80);
	param->setPrecision (1);
	parameters.addParameter (param);

	param = new RangeParameter (USTRING ("Master Tuning"), kParamMasterTuning, USTRING ("cent"),
	                            -200, 200, 0);
	param->setPrecision (0);
	parameters.addParameter (param);

	param = new RangeParameter (USTRING ("Velocity To Level"), kParamVelToLevel, USTRING ("%"), 0,
	                            100, 30);
	param->setPrecision (1);
	parameters.addParameter (param);

	param = new RangeParameter (USTRING ("Release Time"), kParamReleaseTime, USTRING ("sec"), 0.005,
	                            MAX_RELEASE_TIME_SEC, 0.025);
	param->setPrecision (3);
	parameters.addParameter (param);

	param =
	    new RangeParameter (USTRING ("Noise Volume"), kParamNoiseVolume, USTRING ("%"), 0, 100, 0);
	param->setPrecision (1);
	parameters.addParameter (param);
	param =
	    new RangeParameter (USTRING ("Sinus Volume"), kParamSinusVolume, USTRING ("%"), 0, 100, 80);
	param->setPrecision (1);
	parameters.addParameter (param);
	param = new RangeParameter (USTRING ("Triangle Volume"), kParamTriangleVolume, USTRING ("%"), 0,
	                            100, 20);
	param->setPrecision (1);
	parameters.addParameter (param);
	param = new RangeParameter (USTRING ("Square Volume"), kParamSquareVolume, USTRING ("%"), 0,
	                            100, 80);
	param->setPrecision (1);
	parameters.addParameter (param);

	param = new RangeParameter (USTRING ("Sinus Detune"), kParamSinusDetune, USTRING ("cent"), -200,
	                            200, 0);
	param->setPrecision (0);
	parameters.addParameter (param);

	param = new RangeParameter (USTRING ("Triangle Slop"), kParamTriangleSlop, USTRING ("%"), 0,
	                            100, 50);
	param->setPrecision (0);
	parameters.addParameter (param);

	auto* filterTypeParam = new StringListParameter (USTRING ("Filter Type"), kParamFilterType);
	filterTypeParam->appendString (USTRING ("Lowpass"));
	filterTypeParam->appendString (USTRING ("Highpass"));
	filterTypeParam->appendString (USTRING ("Bandpass"));
	parameters.addParameter (filterTypeParam);

	param = new LogScaleParameter<ParamValue> (USTRING ("Filter Frequency"), kParamFilterFreq,
	                                           VoiceStatics::freqLogScale);
	param->getInfo ().defaultNormalizedValue = 0.75;
	param->setPrecision (1);
	parameters.addParameter (param);

	param = new RangeParameter (USTRING ("Frequency Mod Depth"), kParamFilterFreqModDepth,
	                            USTRING ("%"), -100, 100, 20);
	param->setPrecision (1);
	parameters.addParameter (param);

	param = parameters.addParameter (USTRING ("Filter Q"), nullptr, 0, 0,
	                                 ParameterInfo::kCanAutomate, kParamFilterQ);
	param->getInfo ().defaultNormalizedValue = 0.2;
	param->setPrecision (2);

	parameters.addParameter (USTRING ("Bypass SNA"), nullptr, 1, 0, ParameterInfo::kCanAutomate,
	                         kParamBypassSNA);

	parameters.addParameter (new RangeParameter (USTRING ("Active Voices"), kParamActiveVoices,
	                                             nullptr, 0, MAX_VOICES, 0, MAX_VOICES,
	                                             ParameterInfo::kIsReadOnly));

	auto* tuningRangeParam = new StringListParameter (USTRING ("Tuning Range"), kParamTuningRange,
	                                                  nullptr, ParameterInfo::kIsList);
	tuningRangeParam->appendString (USTRING ("[-1, +1] Octave"));
	tuningRangeParam->appendString (USTRING ("[-3, +2] Tunes"));
	parameters.addParameter (tuningRangeParam);

	// Init Note Expression Types
	// Volume definition: plain range[0 = -oo, 0.25 = 0dB, 0.5 = +6dB, 1 = +12dB]
	constexpr NoteExpressionValue kOdBVolume = 0.25;
	auto volumeNoteExp =
	    new NoteExpressionType (kVolumeTypeID, String ("Volume"), String ("Vol"), nullptr,
	                            kNoParentUnitId, kOdBVolume, 0., 1., kStepCountContinuous);
	volumeNoteExp->setPhysicalUITypeID (PhysicalUITypeIDs::kPUIPressure);
	noteExpressionTypes.addNoteExpressionType (volumeNoteExp);

	// Panning
	noteExpressionTypes.addNoteExpressionType (new PanNoteExpressionType ());

	// Tuning
	NoteExpressionType* tuningNoteExpression = new RangeNoteExpressionType (
	    kTuningTypeID, String ("Tuning"), String ("Tun"), String ("Half Tone"), kNoParentUnitId, 0,
	    120, -120, NoteExpressionTypeInfo::kIsBipolar);
	tuningNoteExpression->getInfo ().valueDesc.minimum = 0.5 - VoiceStatics::kNormTuningOneOctave;
	tuningNoteExpression->getInfo ().valueDesc.maximum = 0.5 + VoiceStatics::kNormTuningOneOctave;
	tuningNoteExpression->setPhysicalUITypeID (PhysicalUITypeIDs::kPUIXMovement);
	noteExpressionTypes.addNoteExpressionType (tuningNoteExpression);

	// Sinus Volume
	auto noteExp = new NoteExpressionType (kSinusVolumeTypeID, String ("Sinus Volume"),
	                                       String ("Sin Vol"), String ("%"), kNoParentUnitId,
	                                       getParameterObject (kParamSinusVolume),
	                                       NoteExpressionTypeInfo::kIsAbsolute);
	noteExpressionTypes.addNoteExpressionType (noteExp);

	// Sinus Detune
	noteExpressionTypes.addNoteExpressionType (new NoteExpressionType (
	    kSinusDetuneTypeID, String ("Sinus Detune"), String ("Sin Detune"), String ("Cent"),
	    kNoParentUnitId, getParameterObject (kParamSinusDetune),
	    NoteExpressionTypeInfo::kIsAbsolute | NoteExpressionTypeInfo::kIsBipolar));

	// Triangle Volume
	noteExpressionTypes.addNoteExpressionType (new NoteExpressionType (
	    kTriangleVolumeTypeID, String ("Triangle Volume"), String ("Tri Vol"), String ("%"),
	    kNoParentUnitId, getParameterObject (kParamTriangleVolume),
	    NoteExpressionTypeInfo::kIsAbsolute));

	// Square Volume
	noteExpressionTypes.addNoteExpressionType (new NoteExpressionType (
	    kSquareVolumeTypeID, String ("Square Volume"), String ("Square Vol"), String ("%"),
	    kNoParentUnitId, getParameterObject (kParamSquareVolume),
	    NoteExpressionTypeInfo::kIsAbsolute));

	// Noise Volume
	noteExpressionTypes.addNoteExpressionType (new NoteExpressionType (
	    kNoiseVolumeTypeID, String ("Noise Volume"), String ("Noise Vol"), String ("%"),
	    kNoParentUnitId, getParameterObject (kParamNoiseVolume),
	    NoteExpressionTypeInfo::kIsAbsolute));

	// Filter Modulations
	// Filter Frequency Modulation
	auto rNoteExp = new RangeNoteExpressionType (
	    kFilterFreqModTypeID, String ("Filter Frequency Modulation"), String ("Freq Mod"), nullptr,
	    kNoParentUnitId, 0, -100, 100, NoteExpressionTypeInfo::kIsBipolar, 0);
	rNoteExp->setPhysicalUITypeID (PhysicalUITypeIDs::kPUIYMovement);
	noteExpressionTypes.addNoteExpressionType (rNoteExp);

	// Filter Q Modulation
	noteExpressionTypes.addNoteExpressionType (new RangeNoteExpressionType (
	    kFilterQModTypeID, String ("Filter Q Modulation"), String ("Q Mod"), nullptr,
	    kNoParentUnitId, 0, -100, 100, NoteExpressionTypeInfo::kIsBipolar, 0));

	// Filter Type
	noteExpressionTypes.addNoteExpressionType (new NoteExpressionType (
	    kFilterTypeTypeID, String ("Filter Type"), String ("Flt Type"), nullptr, kNoParentUnitId,
	    getParameterObject (kParamFilterType), NoteExpressionTypeInfo::kIsBipolar));

	// Release Time Modulation
	noteExpressionTypes.addNoteExpressionType (new ReleaseTimeModNoteExpressionType ());

	// Init Default MIDI-CC Map
	midiCCMapping[{CCType::CC, ControllerNumbers::kPitchBend}] = kParamMasterTuning;
	midiCCMapping[{CCType::CC, ControllerNumbers::kCtrlVolume}] = kParamMasterVolume;
	midiCCMapping[{CCType::CC, ControllerNumbers::kCtrlModWheel}] = kParamFilterFreqModDepth;
	midiCCMapping[{CCType::CC, ControllerNumbers::kCtrlFilterCutoff}] = kParamFilterFreq;
	midiCCMapping[{CCType::CC, ControllerNumbers::kCtrlFilterResonance}] = kParamFilterQ;
	midiCCMapping[{CCType::NRPN, 9999}] = kParamTriangleVolume;
	midiCCMapping[{CCType::RPN, 12222}] = kParamSinusVolume;

	return kResultTrue;
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API Controller::terminate ()
{
	noteExpressionTypes.removeAll ();
	return EditController::terminate ();
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API Controller::setComponentState (IBStream* state)
{
	GlobalParameterState gps {};
	tresult result = gps.setState (state);
	if (result == kResultTrue)
	{
		setParamNormalized (kParamMasterVolume, gps.masterVolume);
		setParamNormalized (kParamMasterTuning, (gps.masterTuning + 1) / 2.);
		setParamNormalized (kParamVelToLevel, gps.velToLevel);
		setParamNormalized (kParamFilterFreqModDepth, (gps.freqModDepth + 1) / 2.);

		setParamNormalized (kParamReleaseTime, gps.releaseTime);
		setParamNormalized (kParamNoiseVolume, gps.noiseVolume);
		setParamNormalized (kParamSinusVolume, gps.sinusVolume);
		setParamNormalized (kParamTriangleVolume, gps.triangleVolume);
		setParamNormalized (kParamSquareVolume, gps.squareVolume);

		setParamNormalized (kParamSinusDetune, (gps.sinusDetune + 1) / 2.);
		setParamNormalized (kParamTriangleSlop, gps.triangleSlop);

		setParamNormalized (kParamFilterType,
		                    plainParamToNormalized (kParamFilterType, gps.filterType));
		setParamNormalized (kParamFilterFreq, gps.filterFreq);
		setParamNormalized (kParamFilterQ, gps.filterQ);

		setParamNormalized (kParamBypassSNA, gps.bypassSNA);

		setParamNormalized (kParamTuningRange,
		                    plainParamToNormalized (kParamTuningRange, gps.tuningRange));
	}
	return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API Controller::setParamNormalized (ParamID tag, ParamValue value)
{
	bool newRange = false;
	if (tag == kParamTuningRange && getParamNormalized (tag) != value)
	{
		newRange = true;
		NoteExpressionType* net = noteExpressionTypes.getNoteExpressionType (kTuningTypeID);
		if (value > 0)
		{
			noteExpressionTypes.addNoteExpressionType (new NoteExpressionType (
			    kTriangleSlopeTypeID, String ("Triangle Slope"), String ("Tri Slope"), String ("%"),
				kNoParentUnitId, getParameterObject (kParamTriangleSlop), NoteExpressionTypeInfo::kIsAbsolute));
			if (net)
			{
				net->getInfo ().valueDesc.minimum = 0.5 - 3 * VoiceStatics::kNormTuningOneTune;
				net->getInfo ().valueDesc.maximum = 0.5 + 2 * VoiceStatics::kNormTuningOneTune;
			}
		}
		else
		{
			noteExpressionTypes.removeNoteExpressionType (kTriangleSlopeTypeID);
			if (net)
			{
				net->getInfo ().valueDesc.minimum = 0.5 - VoiceStatics::kNormTuningOneOctave;
				net->getInfo ().valueDesc.maximum = 0.5 + VoiceStatics::kNormTuningOneOctave;
			}
		}
	}

	tresult res = EditController::setParamNormalized (tag, value);

	if (newRange && componentHandler)
		componentHandler->restartComponent (kNoteExpressionChanged);

	return res;
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API Controller::getMidiControllerAssignment (int32 busIndex, int16 channel,
                                                            CtrlNumber midiControllerNumber,
                                                            ParamID& id /*out*/)
{
	if (busIndex == 0 && channel == 0 && midiControllerNumber < kCountCtrlNumber)
	{
		auto mapResult = midiCCMapping.find ({CCType::CC, midiControllerNumber});
		if (mapResult != midiCCMapping.end ())
		{
			id = mapResult->second;
			return kResultTrue;
		}
	}
	return kResultFalse;
}

//-----------------------------------------------------------------------------
bool Controller::hasMatchingCCType (bool midi1, CCType type) const
{
	return ((!midi1) && (type != CCType::CC)) || ((midi1) && (type == CCType::CC));
}

//-----------------------------------------------------------------------------
uint32 Controller::getNumMidiControllerAssignments (BusDirections direction, bool midi1)
{
	if (direction == BusDirections::kOutput)
		return 0;
	uint32 count = 0;
	for (auto&& pid : midiCCMapping)
	{
		if (hasMatchingCCType (midi1, pid.first.first))
			count++;
	}
	return count;
}

//-----------------------------------------------------------------------------
tresult Controller::getMidiControllerAssignments (
    BusDirections direction,
    std::variant<Midi2ControllerParamIDAssignmentList, Midi1ControllerParamIDAssignmentList> list)
{
	uint32 listSize = 0;
	bool midi1 = false;
	if (std::holds_alternative<Midi2ControllerParamIDAssignmentList> (list))
		listSize = std::get<Midi2ControllerParamIDAssignmentList> (list).count;
	else
	{
		listSize = std::get<Midi1ControllerParamIDAssignmentList> (list).count;
		midi1 = true;
	}
	if (direction == BusDirections::kOutput || listSize == 0)
		return kResultFalse;
	uint32 i = 0;
	for (auto&& pid : midiCCMapping)
	{
		if (hasMatchingCCType (midi1, pid.first.first))
		{
			if (i >= listSize)
			{
				SMTG_ASSERT (false); // more than expected
				break;
			}
			if (std::holds_alternative<Midi2ControllerParamIDAssignmentList> (list))
			{
				auto& m2List = std::get<Midi2ControllerParamIDAssignmentList> (list);
				m2List.map[i].busIndex = 0;
				m2List.map[i].channel = 0;
				m2List.map[i].controller = Midi2Controller::create (
				    pid.first.first == CCType::RPN, pid.first.second >> 7, pid.first.second & 0x7F);
				m2List.map[i].pId = pid.second;
			}
			else
			{
				auto& m1List = std::get<Midi1ControllerParamIDAssignmentList> (list);
				m1List.map[i].busIndex = 0;
				m1List.map[i].channel = 0;
				m1List.map[i].controller = pid.first.second;
				m1List.map[i].pId = pid.second;
			}
			i++;
		}
	}
	return kResultTrue;
}

//-----------------------------------------------------------------------------
uint32 PLUGIN_API Controller::getNumMidi2ControllerAssignments (BusDirections direction)
{
	return getNumMidiControllerAssignments (direction, false);
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API Controller::getMidi2ControllerAssignments (
    BusDirections direction, const Midi2ControllerParamIDAssignmentList& list)
{
	return getMidiControllerAssignments (direction, list);
}

//-----------------------------------------------------------------------------
uint32 PLUGIN_API Controller::getNumMidi1ControllerAssignments (BusDirections direction)
{
	return getNumMidiControllerAssignments (direction, true);
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API Controller::getMidi1ControllerAssignments (
    BusDirections direction, const Midi1ControllerParamIDAssignmentList& list)
{
	return getMidiControllerAssignments (direction, list);
}

//-----------------------------------------------------------------------------
int32 PLUGIN_API Controller::getNoteExpressionCount (int32 busIndex, int16 channel)
{
	if (busIndex == 0 && channel == 0)
	{
		return noteExpressionTypes.getNoteExpressionCount ();
	}
	return 0;
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API Controller::getNoteExpressionInfo (int32 busIndex, int16 channel,
                                                      int32 noteExpressionIndex,
                                                      NoteExpressionTypeInfo& info /*out*/)
{
	if (busIndex == 0 && channel == 0)
	{
		return noteExpressionTypes.getNoteExpressionInfo (noteExpressionIndex, info);
	}
	return kResultFalse;
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API Controller::getNoteExpressionStringByValue (
    int32 busIndex, int16 channel, NoteExpressionTypeID id,
    NoteExpressionValue valueNormalized /*in*/, String128 string /*out*/)
{
	if (busIndex == 0 && channel == 0)
	{
		return noteExpressionTypes.getNoteExpressionStringByValue (id, valueNormalized, string);
	}
	return kResultFalse;
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API Controller::getNoteExpressionValueByString (
    int32 busIndex, int16 channel, NoteExpressionTypeID id, const TChar* string /*in*/,
    NoteExpressionValue& valueNormalized /*out*/)
{
	if (busIndex == 0 && channel == 0)
	{
		return noteExpressionTypes.getNoteExpressionValueByString (id, string, valueNormalized);
	}
	return kResultFalse;
}

//-----------------------------------------------------------------------------
tresult PLUGIN_API Controller::getPhysicalUIMapping (int32 busIndex, int16 channel,
                                                     PhysicalUIMapList& list)
{
	if (busIndex == 0 && channel == 0)
	{
		for (uint32 i = 0; i < list.count; ++i)
		{
			NoteExpressionTypeID type = kInvalidTypeID;
			if (noteExpressionTypes.getMappedNoteExpression (list.map[i].physicalUITypeID, type) ==
			    kResultTrue)
				list.map[i].noteExpressionTypeID = type;
		}
		return kResultTrue;
	}
	return kResultFalse;
}

//------------------------------------------------------------------------
} // NoteExpressionSynth
} // Vst
} // Steinberg
