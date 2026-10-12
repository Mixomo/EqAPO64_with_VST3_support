/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  Equalizer APO contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include "IFilter.h"

#pragma AVRT_VTABLES_BEGIN
class OutputGuardFilter : public IFilter
{
public:
	explicit OutputGuardFilter(double ceilingDb);

	bool getInPlace() override { return true; }

	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	double ceilingDb;
	double ceiling;
	double currentGain;
	double releaseCoefficient;
	size_t channelCount;
};
#pragma AVRT_VTABLES_END
