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
#include <cmath>
#include <mutex>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#include <sndfile.h>
#include <fftw3.h>

#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "ConvolutionFilter.h"

using namespace std;

#include "helpers/FFTPlannerLock.h"

ConvolutionFilter::ConvolutionFilter(wstring filename)
{
	this->filename = filename;
	filters = NULL;
	filterFrameCount = 0;
}

ConvolutionFilter::~ConvolutionFilter()
{
	cleanup();
}

vector<wstring> ConvolutionFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	cleanup();

	this->sampleRate = sampleRate;
	this->maxFrameCount = maxFrameCount;
	filterFrameCount = 0;
	channelCount = (unsigned)channelNames.size();

	initializeFilters(maxFrameCount);
	if (filters != NULL)
		filterFrameCount = maxFrameCount;

	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void ConvolutionFilter::process(double** output, double** input, unsigned frameCount)
{
	if (frameCount == 0 || filters == NULL)
		return;

	if (frameCount != filterFrameCount)
	{
		for (unsigned i = 0; i < channelCount; i++)
			if (output[i] != input[i])
				memcpy(output[i], input[i], frameCount * sizeof(double));
		return;
	}

	for (unsigned i = 0; i < channelCount; i++)
	{
		double* inputChannel = input[i];
		double* outputChannel = output[i];
		HConvSingle* filter = &filters[i];

		hcPutSingle(filter, inputChannel);
		hcProcessSingle(filter);
		hcGetSingle(filter, outputChannel);
	}
}
#pragma AVRT_CODE_END

void ConvolutionFilter::cleanup()
{
	if (filters != NULL)
	{
        lock_guard<mutex> plannerLock(fftwPlannerMutex);
		for (unsigned i = 0; i < channelCount; i++)
			hcCloseSingle(&filters[i]);

		MemoryHelper::free(filters);
		filters = NULL;
		filterFrameCount = 0;
	}
}

void ConvolutionFilter::initializeFilters(unsigned frameCount)
{
	SF_INFO info = {};

	SNDFILE* inFile = sf_wchar_open(filename.c_str(), SFM_READ, &info);
	if (inFile == NULL)
	{
		LogF(L"Error while reading impulse response file: %S", sf_strerror(inFile));
	}
	else
	{
		TraceF(L"Convolving using impulse response file %s", filename.c_str());
		unsigned fileChannelCount = info.channels;
		unsigned fileFrameCount = (unsigned)info.frames;
		const int targetSampleRate = static_cast<int>(sampleRate + 0.5f);

		if (fileChannelCount == 0 || fileFrameCount == 0 || info.samplerate <= 0 || sampleRate <= 0.0f)
		{
			LogF(L"Invalid impulse response metadata for %s: %u channels, %u frames, source SR %d, target SR %f",
				filename.c_str(), fileChannelCount, fileFrameCount, info.samplerate, sampleRate);
			sf_close(inFile);
			return;
		}

		TraceF(L"Impulse response metadata: source SR %d Hz, target SR %d Hz, %u channels, %u frames",
			info.samplerate, targetSampleRate, fileChannelCount, fileFrameCount);

		if (info.samplerate != targetSampleRate)
		{
			LogF(L"Impulse response sample rate (%d Hz) does not match device sample rate (%d Hz). Load or export an IR/FIR at the current device sample rate.",
				info.samplerate, targetSampleRate);
			sf_close(inFile);
			return;
		}

		double* interleavedBuf = new double[fileFrameCount * fileChannelCount];

		sf_count_t numRead = 0;
		while (numRead < fileFrameCount)
		{
			sf_count_t read = sf_readf_double(inFile, interleavedBuf + numRead * fileChannelCount, fileFrameCount - numRead);
			if (read <= 0)
				break;
			numRead += read;
		}

		sf_close(inFile);
		inFile = NULL;

		if (numRead <= 0)
		{
			LogF(L"Could not read impulse response samples from %s", filename.c_str());
			delete[] interleavedBuf;
			return;
		}
		if (static_cast<unsigned>(numRead) != fileFrameCount)
		{
			LogF(L"Impulse response file %s was shorter than expected: read %ld of %u frames",
				filename.c_str(), static_cast<long>(numRead), fileFrameCount);
			fileFrameCount = static_cast<unsigned>(numRead);
		}

		double** bufs = new double* [fileChannelCount];
		for (unsigned i = 0; i < fileChannelCount; i++)
		{
			double* buf = new double[fileFrameCount];
			double* p = interleavedBuf + i;
			for (unsigned j = 0; j < fileFrameCount; j++)
			{
				buf[j] = p[j * fileChannelCount];
			}

			bufs[i] = buf;
		}

		filters = (HConvSingle*)MemoryHelper::alloc(sizeof(HConvSingle) * channelCount);
		{
			lock_guard<mutex> plannerLock(fftwPlannerMutex);
			for (unsigned i = 0; i < channelCount; i++)
			{
				hcInitSingle(&filters[i], bufs[i % fileChannelCount], fileFrameCount, frameCount, 1);
			}
		}
		filterFrameCount = frameCount;

		for (unsigned i = 0; i < fileChannelCount; i++)
		{
			delete[] bufs[i];
		}
		delete[] bufs;
		delete[] interleavedBuf;
	}
}
