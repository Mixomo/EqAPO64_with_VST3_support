#pragma once

#include <vector>
#include <string>
#include "IFilter.h"

class CrossfeedFilter : public IFilter
{
public:
	CrossfeedFilter(std::wstring algorithm, double amountPercent, double circumferenceCm, double headWidthCm, double headLengthCm, double angleDeg, double cutoffHz, double directPercent);
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	enum class Algorithm { Natural, BS2B };
	struct Biquad
	{
		double b0 = 1.0;
		double b1 = 0.0;
		double b2 = 0.0;
		double a1 = 0.0;
		double a2 = 0.0;
		double z1 = 0.0;
		double z2 = 0.0;
		void set(double nb0, double nb1, double nb2, double na0, double na1, double na2);
		double process(double x);
		void reset();
	};

	void setupBS2BFilters();
	void setLowShelf(Biquad& filter, double f0, double gainDb, double slope, double postGainDb);
	void setHighShelf(Biquad& filter, double f0, double gainDb, double slope, double postGainDb);
	void setAllPass(Biquad& filter, double delaySeconds);
	double firstOrderLowpass(double input, double& state) const;
	void processNatural(double** output, double** input, unsigned frameCount);
	void processBS2B(double** output, double** input, unsigned frameCount);

	Algorithm algorithm = Algorithm::Natural;
	double amount;
	double circumferenceCm;
	double headWidthCm;
	double headLengthCm;
	double angleDeg;
	double cutoffHz;
	double directGain;
	float sampleRate = 48000.0f;
	unsigned channelCount = 0;
	unsigned delaySamples = 1;
	unsigned writeIndex = 0;
	std::vector<double> leftDelay;
	std::vector<double> rightDelay;
	double leftLp = 0.0;
	double rightLp = 0.0;
	double lpCoeff = 0.0;
	Biquad mainShelf[2];
	Biquad crossShelf[2];
	Biquad crossAllPass[2];
	double bs2bA0Lo = 0.0;
	double bs2bB1Lo = 0.0;
	double bs2bA0Hi = 1.0;
	double bs2bA1Hi = 0.0;
	double bs2bB1Hi = 0.0;
	double bs2bGain = 1.0;
	double bs2bLo[2] = {};
	double bs2bHi[2] = {};
	double bs2bPrevInput[2] = {};
};
