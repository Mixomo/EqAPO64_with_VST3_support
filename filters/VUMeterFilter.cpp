#include "stdafx.h"
#include <algorithm>
#include <cmath>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "VUMeterFilter.h"
#include "AudioToolsHelper.h"

using namespace std;

VUMeterFilter::VUMeterFilter(wstring meterId, wstring channelSelector, wstring rmsStandard, wstring lufsStandard)
	: meterId(meterId.empty() ? L"default" : meterId),
	  channelSelector(channelSelector.empty() ? L"all" : channelSelector),
	  rmsStandard(rmsStandard.empty() ? L"AES17" : rmsStandard),
	  lufsStandard(lufsStandard.empty() ? L"ITU-R BS.1770-5" : lufsStandard)
{
}

VUMeterFilter::~VUMeterFilter()
{
	cleanup();
}

vector<wstring> VUMeterFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	cleanup();
	this->sampleRate = sampleRate > 0.0f ? sampleRate : 48000.0f;
	channelCount = min<unsigned>(static_cast<unsigned>(channelNames.size()), VUMETER_MAX_CHANNELS);
	channels = AudioTools::resolveChannels(channelSelector, channelNames);
	const wstring rmsLower = StringHelper::toLowerCase(rmsStandard);
	rmsScale = rmsLower.find(L"aes17") != wstring::npos ? sqrt(2.0) : 1.0;
	openSharedData();
	return channelNames;
}

wstring VUMeterFilter::objectName() const
{
	wstring safe = meterId;
	for (wchar_t& ch : safe)
		if (!iswalnum(ch) && ch != L'-' && ch != L'_')
			ch = L'_';
	return L"Global\\EqAPO_VUMeter_" + safe;
}

void VUMeterFilter::openSharedData()
{
	PSECURITY_DESCRIPTOR sd = NULL;
	SECURITY_ATTRIBUTES sa = {};
	sa.nLength = sizeof(sa);
	if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)", SDDL_REVISION_1, &sd, NULL))
		sa.lpSecurityDescriptor = sd;

	mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, sd ? &sa : NULL, PAGE_READWRITE, 0, sizeof(VUMeterSharedData), objectName().c_str());
	if (sd)
		LocalFree(sd);
	if (mapping == NULL)
	{
		LogF(L"VUMeter: could not create shared memory for meter %s", meterId.c_str());
		return;
	}
	shared = static_cast<VUMeterSharedData*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VUMeterSharedData)));
	if (shared == NULL)
	{
		CloseHandle(mapping);
		mapping = NULL;
		LogF(L"VUMeter: could not map shared memory for meter %s", meterId.c_str());
		return;
	}
	memset(shared, 0, sizeof(VUMeterSharedData));
	shared->magic = VUMETER_MAGIC;
	shared->version = VUMETER_VERSION;
	shared->channelCount = channelCount;
	shared->sampleRate = static_cast<std::uint32_t>(sampleRate);
}

void VUMeterFilter::cleanup()
{
	if (shared != NULL)
	{
		UnmapViewOfFile(shared);
		shared = NULL;
	}
	if (mapping != NULL)
	{
		CloseHandle(mapping);
		mapping = NULL;
	}
}

#pragma AVRT_CODE_BEGIN
void VUMeterFilter::process(double** output, double** input, unsigned frameCount)
{
	for (unsigned c = 0; c < channelCount; c++)
		if (output[c] != input[c])
			memcpy(output[c], input[c], frameCount * sizeof(double));

	if (shared == NULL || frameCount == 0)
		return;

	if (shared->resetRequest != 0)
	{
		for (unsigned c = 0; c < VUMETER_MAX_CHANNELS; c++)
		{
			shared->peakHold[c] = 0.0;
			shared->clip[c] = 0;
		}
		integratedMean = 0.0;
		integratedWeight = 0.0;
		for (unsigned c = 0; c < VUMETER_MAX_CHANNELS; c++)
		{
			channelIntegratedMean[c] = 0.0;
			channelIntegratedWeight[c] = 0.0;
			shared->channelLufsMomentary[c] = -INFINITY;
			shared->channelLufsShortTerm[c] = -INFINITY;
			shared->channelLufsIntegrated[c] = -INFINITY;
		}
		shared->resetRequest = 0;
	}

	double blockMean = 0.0;
	unsigned activeChannels = 0;
	bool measured[VUMETER_MAX_CHANNELS] = {};
	const double blockSeconds = frameCount / max(1.0f, sampleRate);
	const double momentaryAlpha = exp(-blockSeconds / 0.4);
	const double shortAlpha = exp(-blockSeconds / 3.0);
	auto toLufs = [](double mean) { return mean > 1e-12 ? 10.0 * log10(mean) - 0.691 : -INFINITY; };
	for (unsigned channel : channels)
	{
		if (channel >= channelCount)
			continue;
		measured[channel] = true;
		activeChannels++;
		double peak = 0.0;
		double sumSquares = 0.0;
		for (unsigned i = 0; i < frameCount; i++)
		{
			const double sample = input[channel][i];
			const double absSample = fabs(sample);
			peak = max(peak, absSample);
			sumSquares += sample * sample;
		}
		const double mean = sumSquares / frameCount;
		blockMean += mean;
		shared->peak[channel] = peak;
		shared->rms[channel] = sqrt(mean) * rmsScale;
		shared->peakHold[channel] = max(shared->peakHold[channel] * 0.9995, peak);
		if (peak >= 1.0)
			shared->clip[channel]++;
		channelMomentaryMean[channel] = channelMomentaryMean[channel] * momentaryAlpha + mean * (1.0 - momentaryAlpha);
		channelShortMean[channel] = channelShortMean[channel] * shortAlpha + mean * (1.0 - shortAlpha);
		channelIntegratedMean[channel] = (channelIntegratedMean[channel] * channelIntegratedWeight[channel] + mean * blockSeconds) / max(1e-9, channelIntegratedWeight[channel] + blockSeconds);
		channelIntegratedWeight[channel] += blockSeconds;
		shared->channelLufsMomentary[channel] = toLufs(channelMomentaryMean[channel]);
		shared->channelLufsShortTerm[channel] = toLufs(channelShortMean[channel]);
		shared->channelLufsIntegrated[channel] = toLufs(channelIntegratedMean[channel]);
	}
	for (unsigned channel = 0; channel < VUMETER_MAX_CHANNELS; channel++)
	{
		if (channel >= channelCount || !measured[channel])
		{
			shared->peak[channel] = 0.0;
			shared->rms[channel] = 0.0;
			shared->peakHold[channel] = 0.0;
			channelMomentaryMean[channel] = 0.0;
			channelShortMean[channel] = 0.0;
			channelIntegratedMean[channel] = 0.0;
			channelIntegratedWeight[channel] = 0.0;
			shared->channelLufsMomentary[channel] = -INFINITY;
			shared->channelLufsShortTerm[channel] = -INFINITY;
			shared->channelLufsIntegrated[channel] = -INFINITY;
		}
	}

	if (activeChannels > 0)
		blockMean /= activeChannels;
	momentaryMean = momentaryMean * momentaryAlpha + blockMean * (1.0 - momentaryAlpha);
	shortMean = shortMean * shortAlpha + blockMean * (1.0 - shortAlpha);
	integratedMean = (integratedMean * integratedWeight + blockMean * blockSeconds) / max(1e-9, integratedWeight + blockSeconds);
	integratedWeight += blockSeconds;
	shared->lufsMomentary = toLufs(momentaryMean);
	shared->lufsShortTerm = toLufs(shortMean);
	shared->lufsIntegrated = toLufs(integratedMean);
	shared->sequence++;
}
#pragma AVRT_CODE_END
