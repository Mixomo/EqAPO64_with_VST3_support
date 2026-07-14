#pragma once

#include "IFilter.h"
#include "BiQuad.h"
#include <string>
#include <vector>

#pragma AVRT_VTABLES_BEGIN
class ParametricEQFilter : public IFilter
{
public:
	struct Band
	{
		bool enabled = true;
		BiQuad::Type type = BiQuad::PEAKING;
		double freq = 1000.0;
		double gain = 0.0;
		double q = 1.0;
	};

	explicit ParametricEQFilter(const std::vector<Band>& bands);

	bool getInPlace() override { return true; }
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

	const std::vector<Band>& getBands() const { return bands; }

private:
	std::vector<Band> bands;
	std::vector<std::vector<BiQuad>> filters;
	unsigned channelCount = 0;
};
#pragma AVRT_VTABLES_END
