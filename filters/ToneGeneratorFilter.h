#pragma once

#include <cstdint>
#include <vector>
#include "IFilter.h"

class ToneGeneratorFilter : public IFilter
{
public:
	enum Type { SINE, WHITE, PINK, BROWN, SWEEP };
	enum Mode { REPLACE, MIX };

	ToneGeneratorFilter(bool state, Type type, double frequency, double startFrequency, double endFrequency, double durationSeconds, double levelDb, std::wstring channelSelector, Mode mode);
	bool getAllChannels() override { return true; }
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	double nextSample();
	double nextNoise();

	bool state;
	Type type;
	Mode mode;
	double frequency;
	double startFrequency;
	double endFrequency;
	double durationSeconds;
	double gain;
	std::wstring channelSelector;
	std::vector<unsigned> channels;
	float sampleRate = 48000.0f;
	double phase = 0.0;
	double sweepTime = 0.0;
	double pinkRows[16] = {};
	double brown = 0.0;
	std::uint32_t randomState = 0x12345678u;
	unsigned channelCount = 0;
};
