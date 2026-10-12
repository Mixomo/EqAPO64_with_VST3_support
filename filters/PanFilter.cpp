#include "stdafx.h"
#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
#include "PanFilter.h"
#include "AudioToolsHelper.h"

using namespace std;

PanFilter::PanFilter(double position, double widthPercent)
	: position(max(-100.0, min(100.0, position)) / 100.0), width(max(0.0, min(200.0, widthPercent)) / 100.0)
{
}

vector<wstring> PanFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	channelCount = static_cast<unsigned>(channelNames.size());
	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void PanFilter::process(double** output, double** input, unsigned frameCount)
{
	if (channelCount < 2)
	{
		for (unsigned c = 0; c < channelCount; c++)
			if (output[c] != input[c])
				memcpy(output[c], input[c], frameCount * sizeof(double));
		return;
	}


    const double angle=(position+1.0)*M_PI_4;
    const double leftPan=std::sqrt(2.0)*std::cos(angle);
    const double rightPan=std::sqrt(2.0)*std::sin(angle);
    const double normalization=std::sqrt(2.0/(1.0+width*width));
    for(unsigned i=0;i<frameCount;++i) {
        const double left=input[0][i],right=input[1][i];
        if(position==0.0 && width==1.0) { output[0][i]=left; output[1][i]=right; continue; }
        const double mid=0.5*(left+right),side=0.5*(left-right)*width;
        output[0][i]=(mid+side)*normalization*leftPan;
        output[1][i]=(mid-side)*normalization*rightPan;
    }

	for (unsigned c = 2; c < channelCount; c++)
		if (output[c] != input[c])
			memcpy(output[c], input[c], frameCount * sizeof(double));
}
#pragma AVRT_CODE_END
