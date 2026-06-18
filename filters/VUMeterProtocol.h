#pragma once

#include <cstdint>

static const std::uint32_t VUMETER_MAGIC = 0x4F505556u; // VUPO
static const std::uint32_t VUMETER_VERSION = 2;
static const unsigned VUMETER_MAX_CHANNELS = 16;

struct VUMeterSharedData
{
	std::uint32_t magic;
	std::uint32_t version;
	std::uint32_t channelCount;
	std::uint32_t sampleRate;
	std::uint64_t sequence;
	double peak[VUMETER_MAX_CHANNELS];
	double peakHold[VUMETER_MAX_CHANNELS];
	double rms[VUMETER_MAX_CHANNELS];
	double lufsMomentary;
	double lufsShortTerm;
	double lufsIntegrated;
	double channelLufsMomentary[VUMETER_MAX_CHANNELS];
	double channelLufsShortTerm[VUMETER_MAX_CHANNELS];
	double channelLufsIntegrated[VUMETER_MAX_CHANNELS];
	std::uint32_t clip[VUMETER_MAX_CHANNELS];
	std::uint32_t resetRequest;
};
