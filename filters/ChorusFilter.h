#pragma once

#include <vector>
#include "IFilter.h"

class ChorusFilter : public IFilter
{
public:
	ChorusFilter(double rateHz, double depthMs, double mixPercent, double feedbackPercent);
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	double rateHz;
	double depthMs;
	double mix;
	double feedback;
	float sampleRate = 48000.0f;
	unsigned channelCount = 0;
	unsigned writeIndex = 0;
	double phase = 0.0;
	std::vector<std::vector<double>> delayBuffers;
};
