/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2012  Jonas Thedering

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
#ifdef DEBUG
#include <stdlib.h>
#include <crtdbg.h>
#endif
#include <cstdio>
#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <sndfile.h>
#include <tclap/CmdLine.h>
#include <fftw3.h>

#include "../version.h"
#include "../FilterEngine.h"
#include "../libHybridConv-0.1.1/libHybridConv_eapo.h"
#include "../helpers/LogHelper.h"
#include "../helpers/StringHelper.h"
#include "../helpers/PrecisionTimer.h"
#include "../helpers/MemoryHelper.h"

using namespace std;

static int nextPowerOfTwo(int value)
{
	int result = 1;
	while (result < value)
		result <<= 1;
	return result;
}

static void referenceConvolutionFft(const vector<double>& input, const vector<double>& impulse, vector<double>& output)
{
	const int resultLength = (int)(input.size() + impulse.size() - 1);
	const int fftLength = nextPowerOfTwo(resultLength);

	double* xTime = (double*)fftw_malloc(sizeof(double) * fftLength);
	double* hTime = (double*)fftw_malloc(sizeof(double) * fftLength);
	fftw_complex* xFreq = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (fftLength / 2 + 1));
	fftw_complex* hFreq = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (fftLength / 2 + 1));

	memset(xTime, 0, sizeof(double) * fftLength);
	memset(hTime, 0, sizeof(double) * fftLength);
	memcpy(xTime, input.data(), sizeof(double) * input.size());
	memcpy(hTime, impulse.data(), sizeof(double) * impulse.size());

	fftw_plan xPlan = fftw_plan_dft_r2c_1d(fftLength, xTime, xFreq, FFTW_ESTIMATE);
	fftw_plan hPlan = fftw_plan_dft_r2c_1d(fftLength, hTime, hFreq, FFTW_ESTIMATE);
	fftw_execute(xPlan);
	fftw_execute(hPlan);

	for (int i = 0; i < fftLength / 2 + 1; i++)
	{
		const double real = xFreq[i][0] * hFreq[i][0] - xFreq[i][1] * hFreq[i][1];
		const double imag = xFreq[i][0] * hFreq[i][1] + xFreq[i][1] * hFreq[i][0];
		xFreq[i][0] = real;
		xFreq[i][1] = imag;
	}

	fftw_plan yPlan = fftw_plan_dft_c2r_1d(fftLength, xFreq, xTime, FFTW_ESTIMATE);
	fftw_execute(yPlan);

	output.resize(input.size());
	for (size_t i = 0; i < output.size(); i++)
		output[i] = xTime[i] / fftLength;

	fftw_destroy_plan(yPlan);
	fftw_destroy_plan(hPlan);
	fftw_destroy_plan(xPlan);
	fftw_free(hFreq);
	fftw_free(xFreq);
	fftw_free(hTime);
	fftw_free(xTime);
}

static double deterministicNoise(unsigned& state)
{
	state = state * 1664525u + 1013904223u;
	return ((state >> 8) / 16777216.0) * 2.0 - 1.0;
}

static bool runConvolutionSelfTestCase(int sampleRate, int frameLength, int impulseLength, int blocks)
{
	const int inputLength = frameLength * blocks;
	vector<double> input(inputLength);
	vector<double> impulse(impulseLength);
	vector<double> actual(inputLength);
	vector<double> reference;
	vector<double> block(frameLength);

	unsigned state = 0x12345678u ^ (unsigned)sampleRate ^ (unsigned)frameLength;
	for (int i = 0; i < inputLength; i++)
	{
		const double t = (double)i / sampleRate;
		input[i] = 0.17 * sin(2.0 * M_PI * 997.0 * t)
			+ 0.11 * sin(2.0 * M_PI * 1234.5 * t)
			+ 0.03 * deterministicNoise(state);
	}

	for (int i = 0; i < impulseLength; i++)
	{
		const double decay = exp(-(double)i / (0.065 * sampleRate));
		impulse[i] = decay * (0.012 * sin(0.013 * i) + 0.006 * deterministicNoise(state));
	}

	// Exercise boundaries across several filter partitions.
	if (!impulse.empty())
	{
		impulse[0] += 0.55;
		for (int p = frameLength - 1; p < impulseLength; p += frameLength)
			impulse[p] += 0.04 * ((p / frameLength) % 2 == 0 ? 1.0 : -1.0);
		for (int p = frameLength; p < impulseLength; p += frameLength)
			impulse[p] += 0.035 * ((p / frameLength) % 2 == 0 ? -1.0 : 1.0);
	}

	referenceConvolutionFft(input, impulse, reference);

	HConvSingle filter;
	hcInitSingle(&filter, impulse.data(), impulseLength, frameLength, 1);
	for (int b = 0; b < blocks; b++)
	{
		hcPutSingle(&filter, input.data() + (size_t)b * frameLength);
		hcProcessSingle(&filter);
		hcGetSingle(&filter, block.data());
		memcpy(actual.data() + (size_t)b * frameLength, block.data(), sizeof(double) * frameLength);
	}
	hcCloseSingle(&filter);

	double maxAbsError = 0.0;
	double rmsError = 0.0;
	double maxReference = 0.0;
	int maxIndex = 0;
	for (int i = 0; i < inputLength; i++)
	{
		const double error = fabs(actual[i] - reference[i]);
		if (error > maxAbsError)
		{
			maxAbsError = error;
			maxIndex = i;
		}
		rmsError += error * error;
		maxReference = max(maxReference, fabs(reference[i]));
	}
	rmsError = sqrt(rmsError / inputLength);
	const double relativeError = maxReference > 0.0 ? maxAbsError / maxReference : maxAbsError;
	const bool passed = maxAbsError < 1e-8 || relativeError < 1e-8;

	printf("%s sr=%d flen=%d hlen=%d blocks=%d max=%0.12g rel=%0.12g rms=%0.12g idx=%d\n",
		passed ? "PASS" : "FAIL",
		sampleRate, frameLength, impulseLength, blocks,
		maxAbsError, relativeError, rmsError, maxIndex);

	return passed;
}

static int runConvolutionSelfTest()
{
	struct TestCase
	{
		int sampleRate;
		int frameLength;
		int impulseLength;
		int blocks;
	};

	const TestCase tests[] =
	{
		{ 44100, 256, 8192, 80 },
		{ 48000, 256, 8916, 80 },
		{ 96000, 512, 17832, 64 },
		{ 192000, 1024, 35664, 48 },
		{ 192000, 256, 35664, 80 },
	};

	bool ok = true;
	printf("Running internal HybridConv correctness benchmark...\n");
	for (const TestCase& test : tests)
		ok = runConvolutionSelfTestCase(test.sampleRate, test.frameLength, test.impulseLength, test.blocks) && ok;

	printf("HybridConv correctness benchmark: %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 2;
}

int main(int argc, char** argv)
{
	try
	{
		stringstream versionStream;
		versionStream << MAJOR << "." << MINOR;
		if (REVISION != 0)
			versionStream << "." << REVISION;
		TCLAP::CmdLine cmd("Benchmark generates a linear sine sweep or reads from the given input file. "
			"It then filters the waveform using the Equalizer APO filter configuration "
			"and finally writes to the given file or into the user's temp directory.", ' ', versionStream.str());

		TCLAP::SwitchArg convSelfTestArg("", "convselftest", "Run internal HybridConv correctness benchmark and exit", cmd);
		TCLAP::SwitchArg noPauseArg("", "nopause", "Do not wait for key press at the end", cmd);
		TCLAP::SwitchArg verboseArg("v", "verbose", "Print trace and error messages to console instead of logfile", cmd);
		TCLAP::ValueArg<string> guidArg("", "guid", "Endpoint GUID to use when parsing configuration (Default: <empty>)", false, "", "string", cmd);
		TCLAP::ValueArg<string> connectionnameArg("", "connectionname", "Connection name to use when parsing configuration (Default: File output)", false, "File output", "string", cmd);
		TCLAP::ValueArg<string> devicenameArg("", "devicename", "Device name to use when parsing configuration (Default: Benchmark)", false, "Benchmark", "string", cmd);
		TCLAP::ValueArg<unsigned> batchsizeArg("", "batchsize", "Number of frames processed in one batch (Default: 65536)", false, 65536, "integer", cmd);
		TCLAP::ValueArg<string> outputArg("o", "output", "File to write sound data to", false, "", "string", cmd);
		TCLAP::ValueArg<string> inputArg("i", "input", "File to load sound data from instead of generating sweep", false, "", "string", cmd);
		TCLAP::ValueArg<unsigned> rateArg("r", "rate", "Sample rate of generated sweep (Default: 44100)", false, 44100, "integer", cmd);
		TCLAP::ValueArg<float> toArg("t", "to", "End frequency of generated sweep in Hz (Default: 20000.0)", false, 20000.0f, "float", cmd);
		TCLAP::ValueArg<float> fromArg("f", "from", "Start frequency of generated sweep in Hz (Default: 0.1)", false, 1.0f, "float", cmd);
		TCLAP::ValueArg<float> lengthArg("l", "length", "Length of generated sweep in seconds (Default: 200.0)", false, 200.0f, "float", cmd);
		TCLAP::ValueArg<unsigned> channelArg("c", "channels", "Number of channels of generated sweep (Default: 2)", false, 2, "integer", cmd);

		cmd.parse(argc, argv);

		bool verbose = verboseArg.getValue();
		LogHelper::set(stderr, verbose, true, true);
#ifdef _DEBUG
		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
		// _CrtSetBreakAlloc(3318);
#endif

		if (convSelfTestArg.getValue())
			return runConvolutionSelfTest();

		unsigned sampleRate;
		unsigned channelCount;
		unsigned channelMask;
		unsigned frameCount;
		float length;
		float* buf;

		if (REVISION == 0)
			printf("Benchmark %d.%d\n", MAJOR, MINOR);
		else
			printf("Benchmark %d.%d.%d\n", MAJOR, MINOR, REVISION);

		printf("Run \"%s -h\" to show usage info\n", argv[0]);
		printf("\n");

		string input = inputArg.getValue();
		if (input != "")
		{
			printf("Reading sound data from %s\n", input.c_str());

			PrecisionTimer timer;
			timer.start();

			SF_INFO info = {};
			SNDFILE* inFile = sf_open(input.c_str(), SFM_READ, &info);
			if (inFile == NULL)
			{
				fprintf(stderr, "%s", sf_strerror(inFile));
				return 1;
			}

			sampleRate = info.samplerate;
			channelCount = info.channels;
			channelMask = 0;
			frameCount = (unsigned)info.frames;
			length = float(frameCount) / sampleRate;

			buf = new float[frameCount * channelCount];

			sf_count_t numRead = 0;
			while (numRead < frameCount)
				numRead += sf_readf_float(inFile, buf + numRead * channelCount, frameCount - numRead);

			sf_close(inFile);
			inFile = NULL;

			double readTime = timer.stop();
			printf("Reading input file took %g seconds\n", readTime);
		}
		else
		{
			sampleRate = rateArg.getValue();
			channelMask = 0;
			channelCount = channelArg.getValue();
			float sweepFrom = fromArg.getValue();
			float sweepTo = toArg.getValue();
			float sweepDiff = sweepTo - sweepFrom;
			length = lengthArg.getValue();
			frameCount = (unsigned)(length * sampleRate);

			printf("No input file given, so generating linear sine sweep from %g to %g Hz over %g seconds\n", sweepFrom, sweepTo, length);

			PrecisionTimer timer;
			timer.start();

			buf = new float[frameCount * channelCount];
			for (unsigned i = 0; i < frameCount; i++)
			{
				double t = i * 1.0 / sampleRate;
				float s = (float)sin(((sweepFrom + sweepDiff * (t / length) / 2) * t) * 2 * M_PI);

				for (unsigned j = 0; j < channelCount; j++)
					buf[i * channelCount + j] = s;
			}

			double genTime = timer.stop();
			printf("Generating sweep took %g seconds\n", genTime);
		}

		unsigned batchsize = batchsizeArg.getValue();

		float* buf2 = new float[frameCount * channelCount];
		for (unsigned i = 0; i < frameCount * channelCount; i++)
			buf2[i] = 0.0f;

		PrecisionTimer timer;
		timer.start();
		{
			FilterEngine engine;
			wstring deviceName = StringHelper::toWString(devicenameArg.getValue(), CP_ACP);
			wstring connectionName = StringHelper::toWString(connectionnameArg.getValue(), CP_ACP);
			wstring deviceGuid = StringHelper::toWString(guidArg.getValue(), CP_ACP);
			engine.setDeviceInfo(false, true, deviceName, connectionName, deviceGuid, deviceName + L" " + connectionName + L" " + deviceGuid);
			engine.initialize((float)sampleRate, channelCount, channelCount, channelCount, channelMask, batchsize);

			double initTime = timer.stop();
			if (!verbose)
				printf("\nLoading configuration took %g ms\n", initTime * 1000.0);

			printf("\nProcessing %d frames from %d channel(s)\n", frameCount, channelCount);

			timer.start();

			for (unsigned i = 0; i < frameCount; i += batchsize)
			{
				engine.process(buf2 + i * channelCount, buf + i * channelCount, min(batchsize, frameCount - i));
			}

			double time = timer.stop();

			printf("%d samples processed in %f seconds\n", frameCount * channelCount, time);
			printf("This is equivalent to %.2f%% CPU load (one core) when processing in real time\n", 100.0f * time / length);

			unsigned clipCount = 0;
			float max = 0;
			for (unsigned i = 0; i < frameCount * channelCount; i++)
			{
				float f = fabs(buf2[i]);
				if (f > max)
					max = f;
				if (f > 1.0f)
					clipCount++;
			}

			printf("Max output level: %f (%f dB)", max, log10(max) * 20.0f);
			if (clipCount > 0)
				printf(" (%d samples clipped!)", clipCount);
			printf("\n");

			string output = outputArg.getValue();
			if (output == "")
			{
				char temp[255];
				GetTempPathA(sizeof(temp) / sizeof(wchar_t), temp);

				output = temp;
				output += "testout.wav";
			}

			printf("\nWriting output to %s\n", output.c_str());

			SF_INFO info = {frameCount, (int)sampleRate, (int)channelCount, SF_FORMAT_WAV | SF_FORMAT_PCM_16, 0};
			SNDFILE* outFile = sf_open(output.c_str(), SFM_WRITE, &info);
			if (outFile == NULL)
			{
				fprintf(stderr, "%s", sf_strerror(outFile));
				return 1;
			}

			sf_count_t numWritten = 0;
			while (numWritten < frameCount)
				numWritten += sf_writef_float(outFile, buf2 + numWritten * channelCount, frameCount - numWritten);

			sf_close(outFile);
			outFile = NULL;

			delete[] buf;
			delete[] buf2;
		}

		if (!noPauseArg.getValue())
			system("pause");

		return 0;
	}
	catch (TCLAP::ArgException e)
	{
		printf("Error: %s for arg %s\n", e.error().c_str(), e.argId().c_str());
		return -1;
	}
}
