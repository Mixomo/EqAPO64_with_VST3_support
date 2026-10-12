#pragma once

#include "IFilter.h"

class PanFilter : public IFilter
{
public:
	PanFilter(double position, double widthPercent);

	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	double position;
	double width;
	unsigned channelCount = 0;
};
