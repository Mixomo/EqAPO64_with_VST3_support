#include "stdafx.h"
#include <algorithm>
#include <cmath>
#include "ReverbFilter.h"
#include "AudioToolsHelper.h"

using namespace std;

double ReverbFilter::DelayLine::process(double input)
{
	if (buffer.empty())
		return input;
	const double out = buffer[index];
	buffer[index] = input;
	index++;
	if (index >= buffer.size())
		index = 0;
	return out;
}

ReverbFilter::ReverbFilter(double roomSizePercent, double dampingPercent, double wetPercent, double dryPercent, double widthPercent)
	: roomSize(AudioTools::percentToUnit(roomSizePercent)),
	  damping(AudioTools::percentToUnit(dampingPercent)),
	  wet(AudioTools::percentToUnit(wetPercent)),
	  dry(AudioTools::percentToUnit(dryPercent)),
	  width(AudioTools::percentToUnit(widthPercent))
{
}

vector<wstring> ReverbFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	channelCount = static_cast<unsigned>(channelNames.size());
	const double scale = (sampleRate > 0.0f ? sampleRate : 48000.0f) / 48000.0;
	const int combBase[] = {1116, 1188, 1277, 1356};
	const int allpassBase[] = {556, 441};
	combs.assign(channelCount, vector<DelayLine>(4));
	allpasses.assign(channelCount, vector<DelayLine>(2));
	dampState.assign(channelCount, vector<double>(4, 0.0));
	for (unsigned c = 0; c < channelCount; c++)
	{
		for (unsigned i = 0; i < 4; i++)
			combs[c][i].buffer.assign(max(1, static_cast<int>((combBase[i] + c * 23) * scale)), 0.0);
		for (unsigned i = 0; i < 2; i++)
			allpasses[c][i].buffer.assign(max(1, static_cast<int>((allpassBase[i] + c * 17) * scale)), 0.0);
	}
	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void ReverbFilter::process(double** output, double** input, unsigned frameCount)
{
	if (wet <= 0.0)
	{
		for (unsigned c = 0; c < channelCount; c++)
			if (output[c] != input[c])
				memcpy(output[c], input[c], frameCount * sizeof(double));
		return;
	}

	const double feedback = 0.72 + roomSize * 0.23;
	for (unsigned frame = 0; frame < frameCount; frame++)
	{
		for (unsigned c = 0; c < channelCount; c++)
		{
			double acc = 0.0;
			const double in = input[c][frame];
			for (unsigned i = 0; i < 4; i++)
			{
				double delayed = combs[c][i].process(in + dampState[c][i] * feedback);
				dampState[c][i] = delayed * (1.0 - damping) + dampState[c][i] * damping;
				acc += delayed;
			}
			acc *= 0.25;
			for (unsigned i = 0; i < 2; i++)
			{
				double delayed = allpasses[c][i].process(acc);
				acc = delayed - acc * 0.5;
			}
			output[c][frame] = in * dry + acc * wet;
		}

		if (channelCount >= 2 && width < 1.0)
		{
			const double left = output[0][frame];
			const double right = output[1][frame];
			const double mid = 0.5 * (left + right);
			output[0][frame] = mid + (left - mid) * width;
			output[1][frame] = mid + (right - mid) * width;
		}
	}
}
#pragma AVRT_CODE_END
