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

#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QMenu>
#include <QToolButton>
#include <QCoreApplication>
#include <QDirIterator>
#include <QHBoxLayout>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#include <sndfile.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <fftw3.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <propidl.h>

#include "DeviceAPOInfo.h"
#include "helpers/RegistryHelper.h"
#include "ConvolutionFilterGUI.h"
#include "ui_ConvolutionFilterGUI.h"

static PROPERTYKEY endpointGuidPropertyKey = {{0x1da5d803, 0xd492, 0x4edd, 0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}, 4};

static int nextPowerOfTwo(int value)
{
	int result = 1;
	while (result < value)
		result <<= 1;
	return result;
}

static double interpolateMagnitude(const std::vector<double>& magnitudes, double sourceSampleRate, int sourceFftSize, double frequency)
{
	if (magnitudes.empty())
		return 1.0;

	const double maxKnownFrequency = std::min(20000.0, sourceSampleRate * 0.475);
	const double clampedFrequency = std::max(20.0, std::min(frequency, maxKnownFrequency));
	const double sourceBin = clampedFrequency * sourceFftSize / sourceSampleRate;
	const int lowerBin = std::max(0, std::min(static_cast<int>(std::floor(sourceBin)), static_cast<int>(magnitudes.size()) - 1));
	const int upperBin = std::max(0, std::min(lowerBin + 1, static_cast<int>(magnitudes.size()) - 1));
	const double alpha = sourceBin - lowerBin;
	return magnitudes[lowerBin] + (magnitudes[upperBin] - magnitudes[lowerBin]) * alpha;
}

static bool regenerateFirFromMagnitude(const std::vector<double>& inputData, sf_count_t inputFrames, int channelCount,
	int sourceSampleRate, int targetSampleRate, std::vector<double>& outputData, sf_count_t& outputFrames)
{
	if (sourceSampleRate <= 0 || targetSampleRate <= 0 || inputFrames <= 0 || channelCount <= 0)
		return false;

	const double ratio = static_cast<double>(targetSampleRate) / sourceSampleRate;
	outputFrames = std::max<sf_count_t>(1, static_cast<sf_count_t>(std::llround(inputFrames * ratio)));
	outputData.assign(static_cast<size_t>(outputFrames) * channelCount, 0.0);
	const int targetFftSize = nextPowerOfTwo(static_cast<int>(outputFrames) * 4);
	const int sourceFftSize = nextPowerOfTwo(static_cast<int>(inputFrames) * 4);

	for (int channel = 0; channel < channelCount; channel++)
	{
		double* sourceTime = (double*)fftw_malloc(sizeof(double) * sourceFftSize);
		fftw_complex* sourceFreq = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (sourceFftSize / 2 + 1));
		fftw_complex* logMagnitude = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (targetFftSize / 2 + 1));
		double* cepstrum = (double*)fftw_malloc(sizeof(double) * targetFftSize);
		double* minCepstrum = (double*)fftw_malloc(sizeof(double) * targetFftSize);
		fftw_complex* complexLogSpectrum = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (targetFftSize / 2 + 1));
		fftw_complex* minSpectrum = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (targetFftSize / 2 + 1));
		double* targetTime = (double*)fftw_malloc(sizeof(double) * targetFftSize);

		if (sourceTime == nullptr || sourceFreq == nullptr || logMagnitude == nullptr || cepstrum == nullptr
			|| minCepstrum == nullptr || complexLogSpectrum == nullptr || minSpectrum == nullptr || targetTime == nullptr)
		{
			fftw_free(targetTime);
			fftw_free(minSpectrum);
			fftw_free(complexLogSpectrum);
			fftw_free(minCepstrum);
			fftw_free(cepstrum);
			fftw_free(logMagnitude);
			fftw_free(sourceFreq);
			fftw_free(sourceTime);
			return false;
		}

		memset(sourceTime, 0, sizeof(double) * sourceFftSize);
		for (sf_count_t i = 0; i < inputFrames; i++)
			sourceTime[i] = inputData[static_cast<size_t>(i) * channelCount + channel];

		fftw_plan sourcePlan = fftw_plan_dft_r2c_1d(sourceFftSize, sourceTime, sourceFreq, FFTW_ESTIMATE);
		fftw_plan cepstrumPlan = fftw_plan_dft_c2r_1d(targetFftSize, logMagnitude, cepstrum, FFTW_ESTIMATE);
		fftw_plan logSpectrumPlan = fftw_plan_dft_r2c_1d(targetFftSize, minCepstrum, complexLogSpectrum, FFTW_ESTIMATE);
		fftw_plan targetPlan = fftw_plan_dft_c2r_1d(targetFftSize, minSpectrum, targetTime, FFTW_ESTIMATE);
		if (sourcePlan == nullptr || cepstrumPlan == nullptr || logSpectrumPlan == nullptr || targetPlan == nullptr)
		{
			if (targetPlan != nullptr)
				fftw_destroy_plan(targetPlan);
			if (logSpectrumPlan != nullptr)
				fftw_destroy_plan(logSpectrumPlan);
			if (cepstrumPlan != nullptr)
				fftw_destroy_plan(cepstrumPlan);
			if (sourcePlan != nullptr)
				fftw_destroy_plan(sourcePlan);
			fftw_free(targetTime);
			fftw_free(minSpectrum);
			fftw_free(complexLogSpectrum);
			fftw_free(minCepstrum);
			fftw_free(cepstrum);
			fftw_free(logMagnitude);
			fftw_free(sourceFreq);
			fftw_free(sourceTime);
			return false;
		}

		fftw_execute(sourcePlan);

		std::vector<double> sourceMagnitudes(sourceFftSize / 2 + 1);
		for (int i = 0; i <= sourceFftSize / 2; i++)
			sourceMagnitudes[i] = std::max(std::hypot(sourceFreq[i][0], sourceFreq[i][1]), 1e-7);

		for (int i = 0; i <= targetFftSize / 2; i++)
		{
			const double frequency = static_cast<double>(i) * targetSampleRate / targetFftSize;
			logMagnitude[i][0] = std::log(interpolateMagnitude(sourceMagnitudes, sourceSampleRate, sourceFftSize, frequency));
			logMagnitude[i][1] = 0.0;
		}

		fftw_execute(cepstrumPlan);
		for (int i = 0; i < targetFftSize; i++)
			cepstrum[i] /= targetFftSize;

		memset(minCepstrum, 0, sizeof(double) * targetFftSize);
		minCepstrum[0] = cepstrum[0];
		minCepstrum[targetFftSize / 2] = cepstrum[targetFftSize / 2];
		for (int i = 1; i < targetFftSize / 2; i++)
			minCepstrum[i] = 2.0 * cepstrum[i];

		fftw_execute(logSpectrumPlan);
		for (int i = 0; i <= targetFftSize / 2; i++)
		{
			const double magnitude = std::exp(complexLogSpectrum[i][0]);
			minSpectrum[i][0] = magnitude * std::cos(complexLogSpectrum[i][1]);
			minSpectrum[i][1] = magnitude * std::sin(complexLogSpectrum[i][1]);
		}

		fftw_execute(targetPlan);
		const sf_count_t fadeStart = static_cast<sf_count_t>(outputFrames * 0.82);
		const sf_count_t fadeLength = std::max<sf_count_t>(1, outputFrames - fadeStart);
		for (sf_count_t i = 0; i < outputFrames; i++)
		{
			double window = 1.0;
			if (i >= fadeStart)
			{
				const double x = static_cast<double>(i - fadeStart) / fadeLength;
				window = 0.5 * (1.0 + std::cos(3.14159265358979323846 * x));
			}
			outputData[static_cast<size_t>(i) * channelCount + channel] = (targetTime[i] / targetFftSize) * window;
		}

		fftw_destroy_plan(targetPlan);
		fftw_destroy_plan(logSpectrumPlan);
		fftw_destroy_plan(cepstrumPlan);
		fftw_destroy_plan(sourcePlan);
		fftw_free(targetTime);
		fftw_free(minSpectrum);
		fftw_free(complexLogSpectrum);
		fftw_free(minCepstrum);
		fftw_free(cepstrum);
		fftw_free(logMagnitude);
		fftw_free(sourceFreq);
		fftw_free(sourceTime);
	}

	return true;
}

static double firMagnitudePeak(const std::vector<double>& data, sf_count_t frames, int channelCount)
{
	if (frames <= 0 || channelCount <= 0 || data.empty())
		return 0.0;

	const int fftSize = nextPowerOfTwo(static_cast<int>(frames) * 4);
	double peak = 0.0;
	for (int channel = 0; channel < channelCount; channel++)
	{
		double* time = (double*)fftw_malloc(sizeof(double) * fftSize);
		fftw_complex* freq = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (fftSize / 2 + 1));
		if (time == nullptr || freq == nullptr)
		{
			fftw_free(freq);
			fftw_free(time);
			return peak;
		}
		memset(time, 0, sizeof(double) * fftSize);
		for (sf_count_t i = 0; i < frames; i++)
			time[i] = data[static_cast<size_t>(i) * channelCount + channel];
		fftw_plan plan = fftw_plan_dft_r2c_1d(fftSize, time, freq, FFTW_ESTIMATE);
		if (plan != nullptr)
		{
			fftw_execute(plan);
			for (int i = 0; i <= fftSize / 2; i++)
				peak = std::max(peak, std::hypot(freq[i][0], freq[i][1]));
			fftw_destroy_plan(plan);
		}
		fftw_free(freq);
		fftw_free(time);
	}
	return peak;
}

ConvolutionFilterGUI::ConvolutionFilterGUI(const QString& configPath, unsigned deviceSampleRate, const QString& deviceGuid, const QString& path)
	: ui(new Ui::ConvolutionFilterGUI), deviceGuid(deviceGuid), deviceSampleRate(deviceSampleRate)
{
	ui->setupUi(this);

	this->configPath = configPath;
	ui->pathLineEdit->setText(path);

	bundledIrButton = new QToolButton(this);
	bundledIrButton->setText(tr("Bundled IR/FIR"));
	bundledIrButton->setPopupMode(QToolButton::InstantPopup);
	ui->gridLayout->addWidget(new QLabel(tr("Bundled IR/FIR:"), this), 1, 0);

	QWidget* bundledIrNavWidget = new QWidget(this);
	QHBoxLayout* bundledIrNavLayout = new QHBoxLayout(bundledIrNavWidget);
	bundledIrNavLayout->setContentsMargins(0, 0, 0, 0);
	bundledIrNavLayout->setSpacing(2);
	QToolButton* previousBundledIrButton = new QToolButton(bundledIrNavWidget);
	previousBundledIrButton->setText("<");
	previousBundledIrButton->setToolTip(tr("Previous bundled IR/FIR"));
	QToolButton* nextBundledIrButton = new QToolButton(bundledIrNavWidget);
	nextBundledIrButton->setText(">");
	nextBundledIrButton->setToolTip(tr("Next bundled IR/FIR"));
	bundledIrNavLayout->addWidget(bundledIrButton);
	bundledIrNavLayout->addWidget(previousBundledIrButton);
	bundledIrNavLayout->addWidget(nextBundledIrButton);
	bundledIrNavLayout->addStretch(1);
	ui->gridLayout->addWidget(bundledIrNavWidget, 1, 1, 1, 3);

	QLabel* bundledIrCreditLabel = new QLabel(tr("Bundled IRs are free to use, but their copyright belongs to Greg Hopkins (Hopkins Media Services) and Aleksey Vaneev (Voxengo)."), this);
	bundledIrCreditLabel->setWordWrap(true);
	ui->gridLayout->addWidget(bundledIrCreditLabel, 2, 0, 1, 4);
	populateBundledImpulseResponses();

	connect(ui->matchSampleRatePushButton, &QPushButton::clicked, this, [this]() { matchDeviceSampleRate(true); });
	connect(previousBundledIrButton, &QToolButton::clicked, this, [this]() { selectBundledImpulseAt(currentBundledImpulseIndex() - 1); });
	connect(nextBundledIrButton, &QToolButton::clicked, this, [this]() { selectBundledImpulseAt(currentBundledImpulseIndex() + 1); });
	connect(ui->pathLineEdit, &QLineEdit::textChanged, this, [this]() { updateFileInfo(); });
	QPushButton* resetButton = new QPushButton(tr("Reset"), this);
	ui->gridLayout->addWidget(resetButton, 0, 4);
	connect(resetButton, &QPushButton::clicked, this, [this]() {
		ui->pathLineEdit->clear();
		updateFileInfo();
		emit updateModel();
	});

	updateFileInfo();
}

ConvolutionFilterGUI::~ConvolutionFilterGUI()
{
	delete ui;
}

void ConvolutionFilterGUI::store(QString& command, QString& parameters)
{
	command = "Convolution";
	parameters = ui->pathLineEdit->text();
}

void ConvolutionFilterGUI::on_selectFileToolButton_clicked()
{
	QFileInfo fileInfo(configPath);
	QDir configDir = fileInfo.absoluteDir();
	QString path = ui->pathLineEdit->text();
	if (path.length() > 0)
		fileInfo.setFile(configDir, path);

	QFileDialog dialog(this, tr("Select impulse response file"), fileInfo.absolutePath(), "*.wav;*.flac;*.ogg");
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(tr("Impulse response (*.wav *.flac *.ogg)"));
	if (path.length() > 0)
		dialog.selectFile(fileInfo.fileName());
	if (dialog.exec() == QDialog::Accepted)
	{
		QString absolutePath = dialog.selectedFiles().first();
		QString relativePath = configDir.relativeFilePath(absolutePath);
		if (relativePath.startsWith("../../"))
			relativePath = absolutePath;
		ui->pathLineEdit->setText(QDir::toNativeSeparators(relativePath));
		updateFileInfo();

		emit updateModel();
	}
}

void ConvolutionFilterGUI::on_pathLineEdit_editingFinished()
{
	updateFileInfo();

	emit updateModel();
}

QString ConvolutionFilterGUI::absoluteImpulsePath() const
{
	QString path = ui->pathLineEdit->text();
	if (path.isEmpty())
		return QString();

	QFileInfo configInfo(configPath);
	QDir configDir = configInfo.absoluteDir();
	QFileInfo fileInfo(configDir, path);
	if (!fileInfo.exists())
		fileInfo.setFile(path);

	return QDir::toNativeSeparators(fileInfo.absoluteFilePath());
}

unsigned ConvolutionFilterGUI::refreshDeviceSampleRate() const
{
	unsigned liveSampleRate = liveDeviceSampleRate();
	if (liveSampleRate != 0)
		return liveSampleRate;

	if (!deviceGuid.isEmpty())
	{
		DeviceAPOInfo info;
		if (info.load(deviceGuid.toStdWString()))
			return info.getSampleRate();
	}

	return deviceSampleRate;
}

unsigned ConvolutionFilterGUI::liveDeviceSampleRate() const
{
	if (deviceGuid.isEmpty())
		return 0;

	HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	bool shouldUninitialize = SUCCEEDED(coInit);
	if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE)
		return 0;

	unsigned result = 0;
	IMMDeviceEnumerator* enumerator = nullptr;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
	if (SUCCEEDED(hr))
	{
		IMMDeviceCollection* collection = nullptr;
		hr = enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_UNPLUGGED, &collection);
		if (SUCCEEDED(hr))
		{
			UINT count = 0;
			collection->GetCount(&count);
			for (UINT i = 0; i < count && result == 0; i++)
			{
				IMMDevice* device = nullptr;
				if (FAILED(collection->Item(i, &device)))
					continue;

				IPropertyStore* properties = nullptr;
				if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)))
				{
					PROPVARIANT value;
					PropVariantInit(&value);
					if (SUCCEEDED(properties->GetValue(endpointGuidPropertyKey, &value)) && value.vt == VT_LPWSTR
						&& QString::fromWCharArray(value.pwszVal).compare(deviceGuid, Qt::CaseInsensitive) == 0)
					{
						IAudioClient* audioClient = nullptr;
						if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&audioClient))))
						{
							WAVEFORMATEX* mixFormat = nullptr;
							if (SUCCEEDED(audioClient->GetMixFormat(&mixFormat)) && mixFormat != nullptr)
							{
								result = mixFormat->nSamplesPerSec;
								CoTaskMemFree(mixFormat);
							}
							audioClient->Release();
						}
					}
					PropVariantClear(&value);
					properties->Release();
				}
				device->Release();
			}
			collection->Release();
		}
		enumerator->Release();
	}

	if (shouldUninitialize)
		CoUninitialize();

	return result;
}

void ConvolutionFilterGUI::populateBundledImpulseResponses()
{
	if (bundledIrButton == nullptr)
		return;

	bundledImpulsePaths.clear();
	const QDir irDir(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("IRs"));
	if (!irDir.exists())
		return;

	const QStringList filters = QStringList() << "*.wav" << "*.flac" << "*.ogg";
	QFileInfoList allFiles;
	QDirIterator it(irDir.absolutePath(), filters, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext())
		allFiles.append(QFileInfo(it.next()));
	std::sort(allFiles.begin(), allFiles.end(), [&irDir](const QFileInfo& a, const QFileInfo& b) {
		return irDir.relativeFilePath(a.absoluteFilePath()).compare(irDir.relativeFilePath(b.absoluteFilePath()), Qt::CaseInsensitive) < 0;
	});
	QMenu* rootMenu = new QMenu(bundledIrButton);
	for (const QFileInfo& file : allFiles)
	{
		const QString absolutePath = QDir::toNativeSeparators(file.absoluteFilePath());
		bundledImpulsePaths.append(absolutePath);
		const QStringList parts = irDir.relativeFilePath(file.absoluteFilePath()).split('/', Qt::SkipEmptyParts);
		QMenu* parent = rootMenu;
		for (int i = 0; i < parts.size(); ++i)
		{
			if (i == parts.size() - 1)
			{
				QAction* action = parent->addAction(parts[i]);
				connect(action, &QAction::triggered, this, [this, absolutePath]() { selectBundledImpulseResponse(absolutePath); });
				break;
			}
			QMenu* child = nullptr;
			for (QAction* action : parent->actions())
			{
				if (action->menu() != nullptr && action->text() == parts[i])
				{
					child = action->menu();
					break;
				}
			}
			if (child == nullptr)
				child = parent->addMenu(parts[i]);
			parent = child;
		}
	}
	bundledIrButton->setMenu(rootMenu);
}

void ConvolutionFilterGUI::selectBundledImpulseResponse(const QString& absolutePath)
{
	if (absolutePath.isEmpty())
		return;

	for (int i = 0; i < bundledImpulsePaths.size(); i++)
	{
		if (bundledImpulsePaths[i].compare(QDir::toNativeSeparators(absolutePath), Qt::CaseInsensitive) == 0)
		{
			currentBundledImpulseListIndex = i;
			break;
		}
	}

	QFileInfo configInfo(configPath);
	const QString relativePath = configInfo.absoluteDir().relativeFilePath(absolutePath);
	ui->pathLineEdit->setText(QDir::toNativeSeparators(relativePath.startsWith("..") ? absolutePath : relativePath));
	updateFileInfo();
	matchDeviceSampleRate(false);
	emit updateModel();
}

void ConvolutionFilterGUI::selectBundledImpulseAt(int index)
{
	if (bundledImpulsePaths.isEmpty())
		return;

	if (index < 0)
		index = bundledImpulsePaths.size() - 1;
	else if (index >= bundledImpulsePaths.size())
		index = 0;

	currentBundledImpulseListIndex = index;
	selectBundledImpulseResponse(bundledImpulsePaths[index]);
}

int ConvolutionFilterGUI::currentBundledImpulseIndex()
{
	const QString currentPath = absoluteImpulsePath();
	if (!currentPath.isEmpty())
	{
		for (int i = 0; i < bundledImpulsePaths.size(); i++)
		{
			if (bundledImpulsePaths[i].compare(currentPath, Qt::CaseInsensitive) == 0)
			{
				currentBundledImpulseListIndex = i;
				return i;
			}
		}
	}

	return currentBundledImpulseListIndex;
}

bool ConvolutionFilterGUI::matchDeviceSampleRate(bool interactive)
{
	unsigned targetSampleRate = refreshDeviceSampleRate();
	if (targetSampleRate == 0)
	{
		if (interactive)
			QMessageBox::warning(this, tr("Convolution"), tr("Could not determine the current device sample rate."));
		return false;
	}

	QString inputPath = absoluteImpulsePath();
	if (inputPath.isEmpty() || !QFileInfo::exists(inputPath))
	{
		if (interactive)
			QMessageBox::warning(this, tr("Convolution"), tr("Select an IR/FIR file first."));
		return false;
	}

	SF_INFO inputInfo = {};
	SNDFILE* inputFile = sf_wchar_open(inputPath.toStdWString().c_str(), SFM_READ, &inputInfo);
	if (inputFile == nullptr)
	{
		if (interactive)
			QMessageBox::warning(this, tr("Convolution"), tr("Unsupported IR/FIR file."));
		return false;
	}

	if (inputInfo.channels <= 0 || inputInfo.frames <= 0 || inputInfo.samplerate <= 0)
	{
		sf_close(inputFile);
		if (interactive)
			QMessageBox::warning(this, tr("Convolution"), tr("The IR/FIR file has invalid metadata."));
		return false;
	}

	std::vector<double> inputData(static_cast<size_t>(inputInfo.frames) * inputInfo.channels);
	sf_count_t framesRead = 0;
	while (framesRead < inputInfo.frames)
	{
		sf_count_t read = sf_readf_double(inputFile, inputData.data() + framesRead * inputInfo.channels, inputInfo.frames - framesRead);
		if (read <= 0)
			break;
		framesRead += read;
	}
	sf_close(inputFile);

	if (framesRead <= 0)
	{
		if (interactive)
			QMessageBox::warning(this, tr("Convolution"), tr("Could not read the IR/FIR samples."));
		return false;
	}
	if (framesRead != inputInfo.frames)
		inputData.resize(static_cast<size_t>(framesRead) * inputInfo.channels);

	std::vector<double> outputData;
	sf_count_t outputFrames = framesRead;
	if (inputInfo.samplerate == static_cast<int>(targetSampleRate))
	{
		outputData = inputData;
	}
	else
	{
		if (!regenerateFirFromMagnitude(inputData, framesRead, inputInfo.channels, inputInfo.samplerate,
			static_cast<int>(targetSampleRate), outputData, outputFrames))
		{
			if (interactive)
				QMessageBox::warning(this, tr("Convolution"), tr("Could not regenerate a matched FIR from the loaded IR/FIR magnitude response."));
			return false;
		}
	}
	const double magnitudePeak = firMagnitudePeak(outputData, outputFrames, inputInfo.channels);
	if (inputInfo.samplerate == static_cast<int>(targetSampleRate) && magnitudePeak > 0.999 && magnitudePeak < 1.001)
	{
		if (interactive)
			QMessageBox::information(this, tr("Convolution"), tr("The loaded IR/FIR already matches the current device sample rate and peak level."));
		return true;
	}
	if (magnitudePeak > 0.0)
		for (double& sample : outputData)
			sample /= magnitudePeak;

	QFileInfo inputFileInfo(inputPath);
	QDir configDir(QFileInfo(configPath).absoluteDir());
	QDir generatedDir(configDir.absoluteFilePath("generated-ir"));
	generatedDir.mkpath(".");
	QString outputPath = generatedDir.absoluteFilePath(
		QString("%1_matched_normalized_%2Hz.wav").arg(inputFileInfo.completeBaseName()).arg(targetSampleRate));

	SF_INFO outputInfo = {};
	outputInfo.channels = inputInfo.channels;
	outputInfo.samplerate = static_cast<int>(targetSampleRate);
	outputInfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	SNDFILE* outputFile = sf_wchar_open(outputPath.toStdWString().c_str(), SFM_WRITE, &outputInfo);
	if (outputFile == nullptr)
	{
		if (interactive)
			QMessageBox::warning(this, tr("Convolution"), tr("Could not create the regenerated matched FIR file."));
		return false;
	}
	sf_count_t framesWritten = sf_writef_double(outputFile, outputData.data(), outputFrames);
	sf_close(outputFile);
	if (framesWritten != outputFrames)
	{
		if (interactive)
			QMessageBox::warning(this, tr("Convolution"), tr("Could not write the complete regenerated matched FIR file."));
		return false;
	}

	QString relativePath = configDir.relativeFilePath(outputPath);
	if (relativePath.startsWith(".."))
		relativePath = outputPath;
	ui->pathLineEdit->setText(QDir::toNativeSeparators(relativePath));
	deviceSampleRate = targetSampleRate;
	updateFileInfo();
	emit updateModel();
	return true;
}

void ConvolutionFilterGUI::updateFileInfo()
{
	bool labelsVisible = true;
	QString error = "";
	const unsigned currentDeviceSampleRate = refreshDeviceSampleRate();
	if (currentDeviceSampleRate != 0)
		deviceSampleRate = currentDeviceSampleRate;

	QString path = ui->pathLineEdit->text();
	if (path.length() == 0)
	{
		error = tr("No file selected");
		labelsVisible = false;
	}
	else
	{
		QFileInfo fileInfo(configPath);
		QDir configDir = fileInfo.absoluteDir();
		fileInfo.setFile(configDir, path);
		if (!fileInfo.exists())
		{
			error = tr("File not found");
			labelsVisible = false;
		}
		else
		{
			path = QDir::toNativeSeparators(fileInfo.absoluteFilePath());

			ACCESS_MASK mask = GENERIC_READ;
			try
			{
				mask = RegistryHelper::getFileAccessForUser(path.toStdWString(), SECURITY_LOCAL_SERVICE_RID);
			}
			catch (RegistryException e)
			{
				// ignore
			}

			if ((mask & GENERIC_READ) != GENERIC_READ && (mask & FILE_GENERIC_READ) != FILE_GENERIC_READ)
			{
				error = tr("The file is not readable for the audio service.\nChange the file permissions or copy the file to the config directory.");
				labelsVisible = false;
			}
			else
			{
				SF_INFO info = {};
				SNDFILE* file = sf_wchar_open(path.toStdWString().c_str(), SFM_READ, &info);
				if (file == NULL)
				{
					error = tr("Unsupported file format");
					labelsVisible = false;
				}
				else
				{
					int sampleRate = info.samplerate;
					double length = info.frames * 1000.0 / sampleRate;

					ui->labelLengthValue->setText(tr("%0 ms (%1 samples)").arg(length).arg(info.frames));
					ui->labelSampleRateValue->setText(tr("%0 Hz").arg(sampleRate));
					sf_close(file);

					if (sampleRate != deviceSampleRate)
					{
						error = tr("The loaded IR/FIR sample rate does not match the current device sample rate (%0 Hz). Creating a matched, normalized FIR...").arg(deviceSampleRate);
						if (!autoMatchingSampleRate)
						{
							autoMatchingSampleRate = true;
							const bool matched = matchDeviceSampleRate(false);
							autoMatchingSampleRate = false;
							if (matched)
								return;
							error = tr("The loaded IR/FIR sample rate does not match the current device sample rate (%0 Hz), and automatic matching failed. Export a native FIR from GraphicEQ or choose a matching IR/FIR.").arg(deviceSampleRate);
						}
					}
				}
			}
		}
	}

	ui->labelLength->setVisible(labelsVisible);
	ui->labelLengthValue->setVisible(labelsVisible);
	ui->labelSampleRate->setVisible(labelsVisible);
	ui->labelSampleRateValue->setVisible(labelsVisible);
	ui->labelError->setVisible(error.length() > 0);
	ui->labelError->setText(error);
}
