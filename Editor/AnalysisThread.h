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

#pragma once

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <fftw3.h>

#include "DeviceAPOInfo.h"
#include <atomic>
#include <cstdint>

class AnalysisThread : public QThread
{
	Q_OBJECT

public:
	AnalysisThread();
	~AnalysisThread();
	void setParameters(std::shared_ptr<AbstractAPOInfo> device, int channelMask, int channelIndex, QString configPath, int frameCount);
	void beginGetResult();
	void endGetResult();

	fftw_complex* getFreqData() const;
	int getFreqDataLength() const;
	int getFreqDataSampleRate() const;
	double getPeakGain() const;
	int getLatency() const;
	double getInitializationTime() const;
	double getProcessingTime() const;
    double getProcessingCpuTime() const {return processingCpuTime;}
	unsigned getProcessedFrames() const;
    unsigned getAnalysisStatus() const {return analysisStatus;}
    std::uint64_t getResultRevision() const {return resultRevision;}
    std::uint64_t getRequestRevision() const {return requestRevision.load(std::memory_order_acquire);}

signals:
	void analysisFinished();

protected:
	void run() override;

private:
	QMutex mutex;
	QWaitCondition condition;
    std::atomic<bool> quit{false};
    std::atomic<std::uint64_t> requestRevision{0};
    unsigned analysisStatus=0;
    std::uint64_t resultRevision=0;

	// input
	std::shared_ptr<AbstractAPOInfo> device;
	int channelMask;
	int channelIndex;
	QString configPath;
	int frameCount = 0;

	// output
	fftw_complex* resultFreqData = NULL;
	int freqDataLength = 0;
	int freqDataSampleRate;
	double peakGain;
	int latency;
	double initializationTime;
	double processingTime;
    double processingCpuTime=0;
	int processedFrames;

	// internal (not protected by mutex)
	int lastFrameCount = -1;
	int lastChannelCount = -1;
	double* buf = NULL;
	double* buf2 = NULL;
	double* timeData = NULL;
	fftw_complex* freqData = NULL;
	fftw_plan planForward = NULL;
};
