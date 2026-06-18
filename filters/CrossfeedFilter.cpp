#include "stdafx.h"
#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
#include <cstring>
#include "helpers/StringHelper.h"
#include "CrossfeedFilter.h"

using namespace std;

static double dbToGain(double db)
{
	return pow(10.0, db / 20.0);
}

void CrossfeedFilter::Biquad::set(double nb0, double nb1, double nb2, double na0, double na1, double na2)
{
	if (fabs(na0) < 1e-20)
		na0 = 1.0;
	b0 = nb0 / na0;
	b1 = nb1 / na0;
	b2 = nb2 / na0;
	a1 = na1 / na0;
	a2 = na2 / na0;
	reset();
}

double CrossfeedFilter::Biquad::process(double x)
{
	const double y = b0 * x + z1;
	z1 = b1 * x - a1 * y + z2;
	z2 = b2 * x - a2 * y;
	return y;
}

void CrossfeedFilter::Biquad::reset()
{
	z1 = 0.0;
	z2 = 0.0;
}

CrossfeedFilter::CrossfeedFilter(wstring algorithmName, double amountPercent, double circumferenceCm, double headWidthCm, double headLengthCm, double angleDeg, double cutoffHz, double directPercent)
	: amount((std::max)(0.0, (std::min)(100.0, amountPercent)) / 100.0),
	  circumferenceCm((std::max)(45.0, (std::min)(70.0, circumferenceCm))),
	  headWidthCm((std::max)(10.0, (std::min)(22.0, headWidthCm))),
	  headLengthCm((std::max)(14.0, (std::min)(25.0, headLengthCm))),
	  angleDeg((std::max)(10.0, (std::min)(90.0, angleDeg))),
	  cutoffHz((std::max)(200.0, (std::min)(2500.0, cutoffHz))),
	  directGain((std::max)(50.0, (std::min)(120.0, directPercent)) / 100.0)
{
	const wstring lower = StringHelper::toLowerCase(algorithmName);
	if (lower == L"natural" || lower == L"calcurve")
		algorithm = Algorithm::Natural;
	else if (lower == L"bs2b" || lower == L"rme")
		algorithm = Algorithm::BS2B;
	else
		algorithm = Algorithm::Natural;
}

vector<wstring> CrossfeedFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	this->sampleRate = sampleRate > 0.0f ? sampleRate : 48000.0f;
	channelCount = static_cast<unsigned>(channelNames.size());

	const double theta = angleDeg * M_PI / 180.0;
	const double halfHead = headWidthCm / 200.0;
	const double frontOffset = headLengthCm / 200.0;
	const double circumferenceRadius = circumferenceCm / (2.0 * M_PI * 100.0);
	const double speakerDistance = 1.0 + frontOffset;
	const double dFar = sqrt(speakerDistance * speakerDistance + halfHead * halfHead + 2.0 * speakerDistance * halfHead * sin(theta * 0.5));
	const double dNear = sqrt(speakerDistance * speakerDistance + halfHead * halfHead - 2.0 * speakerDistance * halfHead * sin(theta * 0.5));
	const double pathDelay = (dFar - dNear) / 343.0;
	const double headShadowDelay = circumferenceRadius * sin(theta * 0.5) / 343.0;
	const double delaySeconds = (std::max)(0.00005, 0.65 * pathDelay + 0.35 * headShadowDelay);
	delaySamples = (std::max)(1u, static_cast<unsigned>(round(delaySeconds * this->sampleRate)));
	leftDelay.assign((std::max)(delaySamples + 2, static_cast<unsigned>(this->sampleRate * 0.002)), 0.0);
	rightDelay.assign(leftDelay.size(), 0.0);
	writeIndex = 0;
	leftLp = rightLp = 0.0;
	lpCoeff = 1.0 - exp(-2.0 * M_PI * cutoffHz / this->sampleRate);
	setupBS2BFilters();
	return channelNames;
}

void CrossfeedFilter::setLowShelf(Biquad& filter, double f0, double gainDb, double slope, double postGainDb)
{
	const double A = pow(10.0, gainDb / 40.0);
	const double w0 = 2.0 * M_PI * f0 / sampleRate;
	const double cosw0 = cos(w0);
	const double sinw0 = sin(w0);
	const double qInv = sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
	const double beta = sqrt(A) * sinw0 * qInv;
	const double ap1 = A + 1.0;
	const double am1 = A - 1.0;
	double b0 = A * (ap1 - am1 * cosw0 + beta);
	double b1 = 2.0 * A * (am1 - ap1 * cosw0);
	double b2 = A * (ap1 - am1 * cosw0 - beta);
	const double a0 = ap1 + am1 * cosw0 + beta;
	const double a1 = -2.0 * (am1 + ap1 * cosw0);
	const double a2 = ap1 + am1 * cosw0 - beta;
	const double post = dbToGain(postGainDb);
	filter.set(b0 * post, b1 * post, b2 * post, a0, a1, a2);
}

void CrossfeedFilter::setHighShelf(Biquad& filter, double f0, double gainDb, double slope, double postGainDb)
{
	const double A = pow(10.0, gainDb / 40.0);
	const double w0 = 2.0 * M_PI * f0 / sampleRate;
	const double cosw0 = cos(w0);
	const double sinw0 = sin(w0);
	const double qInv = sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
	const double beta = sqrt(A) * sinw0 * qInv;
	const double ap1 = A + 1.0;
	const double am1 = A - 1.0;
	double b0 = A * (ap1 + am1 * cosw0 + beta);
	double b1 = -2.0 * A * (am1 + ap1 * cosw0);
	double b2 = A * (ap1 + am1 * cosw0 - beta);
	const double a0 = ap1 - am1 * cosw0 + beta;
	const double a1 = 2.0 * (am1 - ap1 * cosw0);
	const double a2 = ap1 - am1 * cosw0 - beta;
	const double post = dbToGain(postGainDb);
	filter.set(b0 * post, b1 * post, b2 * post, a0, a1, a2);
}

void CrossfeedFilter::setAllPass(Biquad& filter, double delaySeconds)
{
	const double a = (1.0 - delaySeconds * 0.5 * sampleRate) / (1.0 + delaySeconds * 0.5 * sampleRate);
	filter.set(a * a, 2.0 * a, 1.0, 1.0, 2.0 * a, a * a);
}

void CrossfeedFilter::setupBS2BFilters()
{
	const double feed = (std::max)(10.0, (std::min)(150.0, 45.0 + amount * 50.0)) / 10.0;
	const double fcut = (std::max)(300.0, (std::min)(2000.0, cutoffHz));
	const double gbLo = feed * -5.0 / 6.0 - 3.0;
	const double gbHi = feed / 6.0 - 3.0;
	const double gLo = dbToGain(gbLo);
	const double gHi = 1.0 - dbToGain(gbHi);
	const double fcHi = fcut * pow(2.0, (gbLo - 20.0 * log10((std::max)(1e-9, gHi))) / 12.0);
	double x = exp(-2.0 * M_PI * fcut / sampleRate);
	bs2bB1Lo = x;
	bs2bA0Lo = gLo * (1.0 - x);
	x = exp(-2.0 * M_PI * fcHi / sampleRate);
	bs2bB1Hi = x;
	bs2bA0Hi = 1.0 - gHi * (1.0 - x);
	bs2bA1Hi = -x;
	bs2bGain = 1.0 / (1.0 - gHi + gLo);
	memset(bs2bLo, 0, sizeof(bs2bLo));
	memset(bs2bHi, 0, sizeof(bs2bHi));
	memset(bs2bPrevInput, 0, sizeof(bs2bPrevInput));
}

double CrossfeedFilter::firstOrderLowpass(double input, double& state) const
{
	state += lpCoeff * (input - state);
	return state;
}

#pragma AVRT_CODE_BEGIN
void CrossfeedFilter::process(double** output, double** input, unsigned frameCount)
{
	if (channelCount < 2 || amount <= 0.0001)
	{
		for (unsigned c = 0; c < channelCount; c++)
			if (output[c] != input[c])
				memcpy(output[c], input[c], frameCount * sizeof(double));
		return;
	}

	if (algorithm == Algorithm::Natural)
		processNatural(output, input, frameCount);
	else
		processBS2B(output, input, frameCount);

	for (unsigned c = 2; c < channelCount; c++)
		if (output[c] != input[c])
			memcpy(output[c], input[c], frameCount * sizeof(double));
}
#pragma AVRT_CODE_END

void CrossfeedFilter::processNatural(double** output, double** input, unsigned frameCount)
{
	const double width = 1.0 + amount * (0.42 - 1.0);
	const double crossGain = 0.16 * amount;
	for (unsigned i = 0; i < frameCount; i++)
	{
		const double left = input[0][i];
		const double right = input[1][i];
		const double mid = 0.5 * (left + right);
		const double side = 0.5 * (left - right) * width;
		leftDelay[writeIndex] = firstOrderLowpass(left, leftLp);
		rightDelay[writeIndex] = firstOrderLowpass(right, rightLp);
		const unsigned readIndex = (writeIndex + static_cast<unsigned>(leftDelay.size()) - delaySamples) % static_cast<unsigned>(leftDelay.size());
		output[0][i] = (mid + side + crossGain * rightDelay[readIndex]) * directGain;
		output[1][i] = (mid - side + crossGain * leftDelay[readIndex]) * directGain;
		writeIndex = (writeIndex + 1) % static_cast<unsigned>(leftDelay.size());
	}
}

void CrossfeedFilter::processBS2B(double** output, double** input, unsigned frameCount)
{
	for (unsigned i = 0; i < frameCount; i++)
	{
		const double left = input[0][i];
		const double right = input[1][i];
		bs2bLo[0] = bs2bA0Lo * left + bs2bB1Lo * bs2bLo[0];
		bs2bLo[1] = bs2bA0Lo * right + bs2bB1Lo * bs2bLo[1];
		bs2bHi[0] = bs2bA0Hi * left + bs2bA1Hi * bs2bPrevInput[0] + bs2bB1Hi * bs2bHi[0];
		bs2bHi[1] = bs2bA0Hi * right + bs2bA1Hi * bs2bPrevInput[1] + bs2bB1Hi * bs2bHi[1];
		bs2bPrevInput[0] = left;
		bs2bPrevInput[1] = right;
		const double wetL = (bs2bHi[0] + bs2bLo[1]) * bs2bGain * directGain;
		const double wetR = (bs2bHi[1] + bs2bLo[0]) * bs2bGain * directGain;
		output[0][i] = left + amount * (wetL - left);
		output[1][i] = right + amount * (wetR - right);
	}
}
