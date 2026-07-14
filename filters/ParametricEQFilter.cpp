#include "stdafx.h"
#include "ParametricEQFilter.h"
#ifndef _M_ARM64
#include <immintrin.h>
#endif

ParametricEQFilter::ParametricEQFilter(const std::vector<Band>& bands)
	: bands(bands)
{
}

std::vector<std::wstring> ParametricEQFilter::initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	channelCount = static_cast<unsigned>(channelNames.size());
	filters.clear();
	filters.resize(channelCount);

	for (unsigned channel = 0; channel < channelCount; ++channel)
	{
		for (const Band& band : bands)
		{
			if (!band.enabled || band.freq <= 0.0 || band.q <= 0.0)
				continue;
			filters[channel].push_back(BiQuad(band.type, band.gain, band.freq, sampleRate, band.q, false));
		}
	}

	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void ParametricEQFilter::process(double** output, double** input, unsigned frameCount)
{
#if !defined(_M_ARM64)
	unsigned oldMxcsr = _mm_getcsr();
	_mm_setcsr(oldMxcsr | 0x8040);
#endif

	for (unsigned channel = 0; channel < channelCount; ++channel)
	{
		double* out = output[channel];
		double* in = input[channel];
		std::vector<BiQuad>& chain = filters[channel];
		for (unsigned frame = 0; frame < frameCount; ++frame)
		{
			double sample = in[frame];
			for (BiQuad& biquad : chain)
				sample = biquad.process(sample);
			out[frame] = sample;
		}
	}

#if !defined(_M_ARM64)
	_mm_setcsr(oldMxcsr);
#endif
}
#pragma AVRT_CODE_END
