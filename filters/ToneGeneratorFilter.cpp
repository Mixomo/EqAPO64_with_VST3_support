#include "stdafx.h"
#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
#include "ToneGeneratorFilter.h"
#include "AudioToolsHelper.h"

using namespace std;

ToneGeneratorFilter::ToneGeneratorFilter(bool state, Type type, double frequency, double startFrequency, double endFrequency, double durationSeconds, double levelDb, wstring channelSelector, Mode mode)
	: state(state), type(type), mode(mode), frequency(frequency), startFrequency(startFrequency), endFrequency(endFrequency), durationSeconds(max(0.01, durationSeconds)), gain(AudioTools::dbToGain(levelDb)), channelSelector(channelSelector)
{
}

vector<wstring> ToneGeneratorFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	this->sampleRate = sampleRate > 0.0f ? sampleRate : 48000.0f;
	channelCount = static_cast<unsigned>(channelNames.size());
	channels = AudioTools::resolveChannels(channelSelector, channelNames);
	randomState = AudioTools::fnv1a(channelSelector) ^ 0x9e3779b9u;
	return channelNames;
}

double ToneGeneratorFilter::nextNoise()
{
	randomState = randomState * 1664525u + 1013904223u;
	return (static_cast<double>((randomState >> 8) & 0x00FFFFFF) / 8388607.5) - 1.0;
}

double ToneGeneratorFilter::nextSample()
{
	if (!state)
		return 0.0;

	if (type == WHITE)
		return nextNoise() * gain;
	if (type == PINK)
	{
		double white = nextNoise();
		unsigned index = 0;
		std::uint32_t n = randomState;
		while ((n & 1u) == 0u && index < 15)
		{
			index++;
			n >>= 1;
		}
		pinkRows[index] = white;
		double sum = 0.0;
		for (double row : pinkRows)
			sum += row;
		return (sum / 16.0) * gain * 1.5;
	}
	if (type == BROWN)
	{
		brown = max(-1.0, min(1.0, brown + nextNoise() * 0.02));
		return brown * gain;
	}

	double freq = frequency;
	if (type == SWEEP)
	{
		const double t = fmod(sweepTime, durationSeconds) / durationSeconds;
		const double ratio = max(1.0, endFrequency) / max(1.0, startFrequency);
		freq = startFrequency * pow(ratio, t);
		sweepTime += 1.0 / sampleRate;
	}

	const double sample = sin(phase) * gain;
	phase += 2.0 * M_PI * freq / sampleRate;
	if (phase >= 2.0 * M_PI)
		phase = fmod(phase, 2.0 * M_PI);
	return sample;
}

#pragma AVRT_CODE_BEGIN
void ToneGeneratorFilter::process(double** output, double** input, unsigned frameCount)
{
	for (unsigned c = 0; c < channelCount; c++)
		if (output[c] != input[c])
			memcpy(output[c], input[c], frameCount * sizeof(double));

	if (!state || channels.empty())
		return;

	for (unsigned frame = 0; frame < frameCount; frame++)
	{
		const double sample = nextSample();
		for (unsigned channel : channels)
		{
			if (channel >= channelCount)
				continue;
			if (mode == REPLACE)
				output[channel][frame] = sample;
			else
				output[channel][frame] += sample;
		}
	}
}
#pragma AVRT_CODE_END
