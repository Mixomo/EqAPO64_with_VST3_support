#pragma once

#include <vector>
#include "IFilter.h"

class ReverbFilter : public IFilter
{
public:
	ReverbFilter(double roomSizePercent, double dampingPercent, double wetPercent, double dryPercent, double widthPercent);
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	struct DelayLine
	{
		std::vector<double> buffer;
		unsigned index = 0;
		double process(double input);
	};

	double roomSize;
	double damping;
	double wet;
	double dry;
	double width;
	unsigned channelCount = 0;
	std::vector<std::vector<DelayLine>> combs;
	std::vector<std::vector<DelayLine>> allpasses;
	std::vector<std::vector<double>> dampState;
};
