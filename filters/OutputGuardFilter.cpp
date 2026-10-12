/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  Equalizer APO contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "stdafx.h"
#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>

#include "OutputGuardFilter.h"

using namespace std;

OutputGuardFilter::OutputGuardFilter(double ceilingDb)
	: ceilingDb(ceilingDb), ceiling(pow(10.0, ceilingDb / 20.0)), currentGain(1.0), releaseCoefficient(1.0), channelCount(0)
{
}

vector<wstring> OutputGuardFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	channelCount = channelNames.size();
	const double releaseSeconds = 0.050;
	const double blockSeconds = sampleRate > 0.0f ? maxFrameCount / sampleRate : releaseSeconds;
	releaseCoefficient = exp(-blockSeconds / releaseSeconds);
	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void OutputGuardFilter::process(double** output, double** input, unsigned frameCount)
{
	double peak = 0.0;
	for (size_t channel = 0; channel < channelCount; ++channel)
	{
		for (unsigned frame = 0; frame < frameCount; ++frame)
			peak = max(peak, abs(input[channel][frame]));
	}

	double targetGain = 1.0;
	if (peak > ceiling && peak > 0.0)
		targetGain = ceiling / peak;

	if (targetGain < currentGain)
		currentGain = targetGain;
	else
		currentGain = 1.0 - (1.0 - currentGain) * releaseCoefficient;

	for (size_t channel = 0; channel < channelCount; ++channel)
	{
		for (unsigned frame = 0; frame < frameCount; ++frame)
			output[channel][frame] = input[channel][frame] * currentGain;
	}
}
#pragma AVRT_CODE_END
