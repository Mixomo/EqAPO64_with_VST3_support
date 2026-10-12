#pragma once

#include "IFilter.h"

class HeadphoneCalibrationFilter : public IFilter
{
public:
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	unsigned channelCount = 0;
};
