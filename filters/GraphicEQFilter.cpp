/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2015  Jonas Thedering

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#define _USE_MATH_DEFINES
#include <cmath>
#include <mutex>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#include <sndfile.h>

#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "GraphicEQFilter.h"

using namespace std;

#include "helpers/FFTPlannerLock.h"

GraphicEQFilter::GraphicEQFilter(const std::vector<FilterNode>& nodes, unsigned filterLength)
	: ConvolutionFilter(L""), nodes(nodes), filterLength(filterLength)
{
}

const std::vector<FilterNode>& GraphicEQFilter::getNodes()
{
	return nodes;
}

std::vector<double> GraphicEQFilter::createImpulseResponse(const std::vector<FilterNode>& nodes, unsigned filterLength, float sampleRate)
{
	fftw_complex* timeData = fftw_alloc_complex(filterLength * 2);
	fftw_complex* freqData = fftw_alloc_complex(filterLength * 2);
	fftw_plan planForward;
	fftw_plan planReverse;
	{
		lock_guard<mutex> plannerLock(fftwPlannerMutex);
		planForward = fftw_plan_dft_1d(filterLength * 2, timeData, freqData, FFTW_FORWARD, FFTW_ESTIMATE);
		planReverse = fftw_plan_dft_1d(filterLength * 2, freqData, timeData, FFTW_BACKWARD, FFTW_ESTIMATE);
	}

	GainIterator gainIterator(nodes);
	for (unsigned i = 0; i < filterLength; i++)
	{
		double freq = i * 1.0 * sampleRate / (filterLength * 2);
		double dbGain = gainIterator.gainAt(freq);
		double gain = (double)pow(10.0, dbGain / 20.0);

		freqData[i][0] = gain;
		freqData[i][1] = 0;
		freqData[2 * filterLength - i - 1][0] = gain;
		freqData[2 * filterLength - i - 1][1] = 0;
	}

	mps(timeData, freqData, planForward, planReverse, filterLength);

	fftw_execute(planReverse);

	for (unsigned i = 0; i < 2 * filterLength; i++)
	{
		timeData[i][0] /= 2 * filterLength;
		timeData[i][1] /= 2 * filterLength;
	}

	for (unsigned i = 0; i < filterLength; i++)
	{
		double factor = (double)(0.5 * (1 + cos(2 * M_PI * i * 1.0 / (2 * filterLength))));
		timeData[i][0] *= factor;
		timeData[i][1] *= factor;
	}

	std::vector<double> result(filterLength);
	for (unsigned i = 0; i < filterLength; i++)
	{
		result[i] = timeData[i][0];
	}

	fftw_free(timeData);
	fftw_free(freqData);
    {
        lock_guard<mutex> plannerLock(fftwPlannerMutex);
	    fftw_destroy_plan(planForward);
	    fftw_destroy_plan(planReverse);
    }

	return result;
}

void GraphicEQFilter::initializeFilters(unsigned frameCount)
{
	std::vector<double> impulse = createImpulseResponse(nodes, filterLength, sampleRate);
	double* buf = new double[impulse.size()];
	for (size_t i = 0; i < impulse.size(); i++)
		buf[i] = impulse[i];

	filters = (HConvSingle*)MemoryHelper::alloc(sizeof(HConvSingle) * channelCount);
	for (unsigned i = 0; i < channelCount; i++)
	{
		hcInitSingle(&filters[i], buf, filterLength, frameCount, 1);
	}

	delete[] buf;
}

// Minimum phase spectrum from coefficients
void GraphicEQFilter::mps(fftw_complex* timeData, fftw_complex* freqData, fftw_plan planForward, fftw_plan planReverse, unsigned filterLength)
{
	double threshold = pow(10.0, -100.0 / 20.0);
	double logThreshold = (double)log(threshold);

	for (unsigned i = 0; i < filterLength * 2; i++)
	{
		if (freqData[i][0] < threshold)
			freqData[i][0] = logThreshold;
		else
			freqData[i][0] = log(freqData[i][0]);

		freqData[i][1] = 0;
	}

	fftw_execute(planReverse);

	for (unsigned i = 0; i < filterLength * 2; i++)
	{
		timeData[i][0] /= filterLength * 2;
		timeData[i][1] /= filterLength * 2;
	}

	for (unsigned i = 1; i < filterLength; i++)
	{
		timeData[i][0] += timeData[filterLength * 2 - i][0];
		timeData[i][1] -= timeData[filterLength * 2 - i][1];

		timeData[filterLength * 2 - i][0] = 0;
		timeData[filterLength * 2 - i][1] = 0;
	}
	timeData[filterLength][1] *= -1;

	fftw_execute(planForward);

	for (unsigned i = 0; i < filterLength * 2; i++)
	{
		double eR = exp(freqData[i][0]);
		freqData[i][0] = double(eR * cos(freqData[i][1]));
		freqData[i][1] = double(eR * sin(freqData[i][1]));
	}
}
