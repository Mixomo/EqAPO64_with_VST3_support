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

#include <QElapsedTimer>

#include "FilterEngine.h"
#include "AnalysisThread.h"
#include "helpers/FFTPlannerLock.h"

using namespace std;
static double analysisOwnerCpuMilliseconds()
{
    FILETIME created{},exited{},kernel{},user{};
    if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return 0;
    return ((std::uint64_t(kernel.dwHighDateTime)<<32)+kernel.dwLowDateTime+(std::uint64_t(user.dwHighDateTime)<<32)+user.dwLowDateTime)/10000.0;
}

AnalysisThread::AnalysisThread()
{
}

AnalysisThread::~AnalysisThread()
{
	mutex.lock();
	quit = true;
	condition.wakeAll();
	mutex.unlock();

	wait();

	if (resultFreqData != NULL)
		fftw_free(resultFreqData);

	if (buf != NULL)
		delete[] buf;
	if (buf2 != NULL)
		delete[] buf2;
	if (timeData != NULL)
		fftw_free(timeData);
	if (freqData != NULL)
		fftw_free(freqData);
	if (planForward != NULL)
    {
        std::lock_guard<std::mutex> plannerLock(fftwPlannerMutex);
		fftw_destroy_plan(planForward);
    }
}

void AnalysisThread::setParameters(shared_ptr<AbstractAPOInfo> device, int channelMask, int channelIndex, QString configPath, int frameCount)
{
	QMutexLocker mutexLocker(&mutex);
	this->device = device;
	this->channelMask = channelMask;
	this->channelIndex = channelIndex;
	this->configPath = configPath;
	this->frameCount = frameCount;
    requestRevision.fetch_add(1,std::memory_order_release);

	condition.wakeAll();
}

void AnalysisThread::beginGetResult()
{
	mutex.lock();
}

void AnalysisThread::endGetResult()
{
	mutex.unlock();
}

fftw_complex* AnalysisThread::getFreqData() const
{
	return resultFreqData;
}

int AnalysisThread::getFreqDataLength() const
{
	return freqDataLength;
}

int AnalysisThread::getFreqDataSampleRate() const
{
	return freqDataSampleRate;
}

double AnalysisThread::getPeakGain() const
{
	return peakGain;
}

int AnalysisThread::getLatency() const
{
	return latency;
}

double AnalysisThread::getInitializationTime() const
{
	return initializationTime;
}

double AnalysisThread::getProcessingTime() const
{
	return processingTime;
}

unsigned AnalysisThread::getProcessedFrames() const
{
	return processedFrames;
}

void AnalysisThread::run()
{
	while (true)
	{
		mutex.lock();
		while (!quit && this->frameCount == 0)
			condition.wait(&mutex);
		if (quit)
		{
			mutex.unlock();
			break;
		}

		shared_ptr<AbstractAPOInfo> device = this->device;
		int channelMask = this->channelMask;
		int channelIndex = this->channelIndex;
		QString configPath = this->configPath;
		int frameCount = this->frameCount;
		this->frameCount = 0;
        const auto revision=requestRevision.load(std::memory_order_acquire);
		mutex.unlock();

		QElapsedTimer timer;
		timer.start();

		unsigned channelCount = device ? device->getChannelCount() : 2;
		if (channelMask != 0 && channelMask != (device ? device->getChannelMask() : 0))
		{
			channelCount = 0;
			for (int i = 0; i < 31; i++)
			{
				int channelPos = 1 << i;
				if (channelMask & channelPos)
					channelCount++;
			}
		}
		if (channelCount == 0)
		{
			channelCount = 8;
			channelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND;
		}

		unsigned sampleRate = device ? device->getSampleRate() : 48000;
		if (sampleRate == 0)
			sampleRate = 48000;

		qint64 startTime = timer.nsecsElapsed();

		FilterEngine engine;
		if(device)engine.setDeviceInfo(device->isInput(), true, device->getDeviceName(), device->getConnectionName(), device->getDeviceGuid(), device->getDeviceString());
        else engine.setDeviceInfo(false,true,L"Analysis",L"",L"",L"");
        channelIndex=(std::max)(0,(std::min)(channelIndex,int(channelCount)-1));
        if(frameCount<=0)continue;
        const int processQuantum=(std::min)(frameCount,960);
		engine.initialize(sampleRate, channelCount, channelCount, channelCount, channelMask, processQuantum, configPath.toStdWString());
		double initializationTime = (timer.nsecsElapsed() - startTime) / 1e6;

		if (frameCount != lastFrameCount || channelCount != lastChannelCount)
		{
			if (buf != NULL)
				delete[] buf;
			buf = new double[frameCount * channelCount];
			memset(buf, 0, frameCount * channelCount * sizeof(double));

			if (buf2 != NULL)
				delete[] buf2;
			buf2 = new double[frameCount * channelCount];
		}
memset(buf,0,frameCount*channelCount*sizeof(double));
        memset(buf2,0,frameCount*channelCount*sizeof(double));
		for (unsigned i = 0; i < channelCount; i++)
			buf[i] = 1.0f;

		if (frameCount != lastFrameCount)
		{
			if (timeData != NULL)
				fftw_free(timeData);
			timeData = fftw_alloc_real(frameCount);

			if (freqData != NULL)
				fftw_free(freqData);
			freqData = fftw_alloc_complex(frameCount);

            {
                std::lock_guard<std::mutex> plannerLock(fftwPlannerMutex);
			    if (planForward != NULL)fftw_destroy_plan(planForward);
			    planForward = fftw_plan_dft_r2c_1d(frameCount, timeData, freqData, FFTW_ESTIMATE);
            }
		}

		lastFrameCount = frameCount;
		lastChannelCount = channelCount;

		int latency = 0;
		int startFrame = -1;
		double processingTime = 0.0;
		unsigned processedFrames = 0;
        unsigned analysisStatus=0;bool cancelled=false;
        const double ownerCpuBefore=analysisOwnerCpuMilliseconds(),hostCpuBefore=engine.getAnalysisCpuMilliseconds();
		// stop searching for startFrame after 10 seconds of audio data
		while (processedFrames < 10 * sampleRate)
		{
			qint64 startTime = timer.nsecsElapsed();
            for(int offset=0;offset<frameCount;offset+=processQuantum) {
                if(quit.load(std::memory_order_acquire) || requestRevision.load(std::memory_order_acquire)!=revision) {cancelled=true;break;}
                const int count=(std::min)(processQuantum,frameCount-offset);
                engine.process(buf2+size_t(offset)*channelCount,buf+size_t(offset)*channelCount,count);
                analysisStatus=engine.getAnalysisStatus();
                if(analysisStatus)break;
                if(timer.elapsed()>10000) {analysisStatus=2;break;}
            }
			processingTime += (timer.nsecsElapsed() - startTime) / 1e6;
			processedFrames += frameCount;
            if(analysisStatus || cancelled)break;

			if (startFrame != -1)
			{
				for (int i = 0; i < startFrame; i++)
				{
					timeData[frameCount - startFrame + i] = buf2[i * channelCount + channelIndex];
				}
				break;
			}

			for (int i = 0; i < frameCount; i++)
			{
				double s = buf2[i * channelCount + channelIndex];
				if (abs(s) > 1e-5f)
				{
					startFrame = i;
					break;
				}
			}

			if (startFrame != -1)
			{
				for (int i = 0; i < frameCount - startFrame; i++)
				{
					timeData[i] = buf2[(startFrame + i) * channelCount + channelIndex];
				}

				if (startFrame == 0)
					break;
			}

			if (latency == 0)
			{
				for (unsigned i = 0; i < channelCount; i++)
					buf[i] = 0.0f;
			}

			if (startFrame == -1)
				latency += frameCount;
		}

        if(cancelled)continue;
        const double processingCpuTime=(std::max)(0.0,analysisOwnerCpuMilliseconds()-ownerCpuBefore)+(std::max)(0.0,engine.getAnalysisCpuMilliseconds()-hostCpuBefore);
		double peakGain;
		if (!analysisStatus && startFrame != -1)
		{
			latency += startFrame;

			fftw_execute(planForward);

			peakGain = -DBL_MAX;

			for (int i = 0; i < frameCount / 2; i++)
			{
				double sqrGain = freqData[i][0] * freqData[i][0] + freqData[i][1] * freqData[i][1];
				if (sqrGain > peakGain)
					peakGain = sqrGain;
			}
			peakGain = sqrt(peakGain);
			peakGain = log10(peakGain) * 20.0;
		}
		else
		{
			latency = 0;
			peakGain = analysisStatus ? numeric_limits<double>::quiet_NaN() : -numeric_limits<double>::infinity();
			memset(freqData, 0, frameCount * sizeof(fftw_complex));
		}

		mutex.lock();
        // A request can change after the final DSP quantum, during FFT.
        // Check under the same mutex as setParameters before publishing.
        if(quit.load(std::memory_order_acquire) || requestRevision.load(std::memory_order_acquire)!=revision) {
            mutex.unlock();continue;
        }
		if (this->freqDataLength != frameCount)
		{
			if (resultFreqData != NULL)
				fftw_free(resultFreqData);
			resultFreqData = fftw_alloc_complex(frameCount);
		}
		memcpy(resultFreqData, freqData, frameCount * sizeof(fftw_complex));
		this->freqDataLength = frameCount;
		this->freqDataSampleRate = sampleRate;
		this->latency = latency;
		this->peakGain = peakGain;
		this->initializationTime = initializationTime;
		this->processingTime = processingTime;
        this->processingCpuTime=processingCpuTime;
		this->processedFrames = processedFrames;
        this->analysisStatus=analysisStatus;
        this->resultRevision=revision;
		mutex.unlock();

		qDebug("Analysis took %.1f ms", timer.nsecsElapsed() / 1e6);

		emit analysisFinished();
	}
}
