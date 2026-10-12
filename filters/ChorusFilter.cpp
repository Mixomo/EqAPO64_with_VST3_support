#include "stdafx.h"
#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
#include "ChorusFilter.h"
#include "AudioToolsHelper.h"

using namespace std;

ChorusFilter::ChorusFilter(double rateHz, double depthMs, double mixPercent, double feedbackPercent)
	: rateHz(max(0.01, min(10.0, rateHz))),
	  depthMs(max(0.1, min(50.0, depthMs))),
	  mix(AudioTools::percentToUnit(mixPercent)),
	  feedback(max(-0.95, min(0.95, feedbackPercent / 100.0)))
{
}

vector<wstring> ChorusFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	this->sampleRate = sampleRate > 0 ? sampleRate : 48000.0f;
	channelCount = static_cast<unsigned>(channelNames.size());
	unsigned maxDelay = static_cast<unsigned>(this->sampleRate * (depthMs + 10.0) / 1000.0) + maxFrameCount + 4;
	delayBuffers.assign(channelCount, vector<double>(maxDelay, 0.0));
	writeIndex = 0;
	phase = 0.0;
	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void ChorusFilter::process(double** output, double** input, unsigned frameCount)
{
	if (mix <= 0.0 || delayBuffers.empty())
	{
		for (unsigned c = 0; c < channelCount; c++)
			if (output[c] != input[c])
				memcpy(output[c], input[c], frameCount * sizeof(double));
		return;
	}

	const double phaseInc = 2.0 * M_PI * rateHz / sampleRate;
	for (unsigned i = 0; i < frameCount; i++)
	{
		const double lfo = 0.5 + 0.5 * sin(phase);
		const double delaySamples = 1.0 + lfo * depthMs * sampleRate / 1000.0;
		phase += phaseInc;
		if (phase >= 2.0 * M_PI)
			phase -= 2.0 * M_PI;

		for (unsigned c = 0; c < channelCount; c++)
		{
			vector<double>& buffer = delayBuffers[c];
			const unsigned size = static_cast<unsigned>(buffer.size());
			const double readPos = writeIndex + size - delaySamples;
			const unsigned index0 = static_cast<unsigned>(floor(readPos)) % size;
			const unsigned index1 = (index0 + 1) % size;
			const double frac = readPos - floor(readPos);
			const double delayed = buffer[index0] * (1.0 - frac) + buffer[index1] * frac;
			const double dry = input[c][i];
			output[c][i] = dry * (1.0 - mix) + delayed * mix;
			buffer[writeIndex] = dry + delayed * feedback;
		}
		writeIndex++;
		if (!delayBuffers.empty() && writeIndex >= delayBuffers[0].size())
			writeIndex = 0;
	}
}
#pragma AVRT_CODE_END
