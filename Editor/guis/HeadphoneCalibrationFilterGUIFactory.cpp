#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <complex>
#include <sndfile.h>

#include "filters/GraphicEQFilter.h"
#include "FilterTable.h"
#include "HeadphoneCalibrationFilterGUIFactory.h"

struct SimplePeqBand
{
	double frequency = 1000.0;
	double gain = 0.0;
	double q = 1.0;
};

static double interpolateBandDb(const QVector<HeadphoneCalibrationFilterGUI::Band>& points, double frequency)
{
	if (points.isEmpty())
		return 0.0;
	if (frequency <= points.first().frequency)
		return points.first().gain;
	if (frequency >= points.last().frequency)
		return points.last().gain;

	for (int i = 1; i < points.size(); ++i)
	{
		if (frequency <= points[i].frequency)
		{
			const double x0 = std::log(points[i - 1].frequency);
			const double x1 = std::log(points[i].frequency);
			const double t = (std::log(frequency) - x0) / std::max(1.0e-9, x1 - x0);
			return points[i - 1].gain + (points[i].gain - points[i - 1].gain) * t;
		}
	}
	return points.last().gain;
}

static QVector<HeadphoneCalibrationFilterGUI::Band> defaultPeqGrid()
{
	QVector<HeadphoneCalibrationFilterGUI::Band> grid;
	for (int i = 0; i < 220; ++i)
	{
		const double t = i / 219.0;
		grid.append({20.0 * std::pow(20000.0 / 20.0, t), 0.0});
	}
	return grid;
}

static double peqMagnitudeDb(const SimplePeqBand& band, double frequency, double sampleRate)
{
	const double a = std::pow(10.0, band.gain / 40.0);
	const double clampedFc = std::clamp(band.frequency, 10.0, sampleRate * 0.48);
	const double clampedF = std::clamp(frequency, 10.0, sampleRate * 0.48);
	constexpr double pi = 3.14159265358979323846;
	const double w0 = 2.0 * pi * clampedFc / sampleRate;
	const double w = 2.0 * pi * clampedF / sampleRate;
	const double alpha = std::sin(w0) / (2.0 * std::max(0.05, band.q));

	const double b0 = 1.0 + alpha * a;
	const double b1 = -2.0 * std::cos(w0);
	const double b2 = 1.0 - alpha * a;
	const double a0 = 1.0 + alpha / a;
	const double a1 = -2.0 * std::cos(w0);
	const double a2 = 1.0 - alpha / a;

	const std::complex<double> z1 = std::exp(std::complex<double>(0.0, -w));
	const std::complex<double> z2 = z1 * z1;
	const std::complex<double> numerator = b0 + b1 * z1 + b2 * z2;
	const std::complex<double> denominator = a0 + a1 * z1 + a2 * z2;
	return 20.0 * std::log10(std::max(1.0e-9, std::abs(numerator / denominator)));
}

static QVector<SimplePeqBand> approximateCurveWithFlexPeq(const QVector<HeadphoneCalibrationFilterGUI::Band>& points)
{
	QVector<HeadphoneCalibrationFilterGUI::Band> residual = defaultPeqGrid();
	for (auto& point : residual)
		point.gain = interpolateBandDb(points, point.frequency);

	QVector<SimplePeqBand> result;
	result.reserve(24);
	for (int iteration = 0; iteration < 24; ++iteration)
	{
		int peakIndex = -1;
		double peakMagnitude = 0.0;
		for (int i = 0; i < residual.size(); ++i)
		{
			const double frequency = residual[i].frequency;
			if (frequency < 25.0 || frequency > 18000.0)
				continue;
			const double magnitude = std::abs(residual[i].gain);
			if (magnitude > peakMagnitude)
			{
				peakMagnitude = magnitude;
				peakIndex = i;
			}
		}

		if (peakIndex < 0 || peakMagnitude < 0.35)
			break;

		const double centreFrequency = residual[peakIndex].frequency;
		const double peakDb = residual[peakIndex].gain;
		const double halfMagnitude = peakMagnitude * 0.5;
		int lowerIndex = peakIndex;
		int upperIndex = peakIndex;
		while (lowerIndex > 0 && std::abs(residual[lowerIndex].gain) >= halfMagnitude && residual[lowerIndex].gain * peakDb > 0.0)
			--lowerIndex;
		while (upperIndex + 1 < residual.size() && std::abs(residual[upperIndex].gain) >= halfMagnitude && residual[upperIndex].gain * peakDb > 0.0)
			++upperIndex;

		const double bandwidth = std::max(centreFrequency * 0.04, residual[upperIndex].frequency - residual[lowerIndex].frequency);
		SimplePeqBand band;
		band.frequency = centreFrequency;
		band.gain = std::clamp(peakDb, -12.0, 12.0);
		band.q = std::clamp(centreFrequency / bandwidth, 0.20, 12.0);
		result.append(band);

		for (auto& point : residual)
			point.gain -= peqMagnitudeDb(band, point.frequency, 48000.0);
	}
	return result;
}

HeadphoneCalibrationFilterGUI::HeadphoneCalibrationFilterGUI(const QString& parameters, FilterTable* filterTable)
	: filterTable(filterTable)
{
	QGridLayout* grid = new QGridLayout(this);
	grid->setContentsMargins(6, 4, 6, 4);
	grid->setHorizontalSpacing(10);
	grid->setVerticalSpacing(8);
	setMinimumWidth(1180);

	sourceComboBox = new QComboBox(this);
	brandComboBox = new QComboBox(this);
	modelComboBox = new QComboBox(this);
	addGraphicButton = new QPushButton(tr("Add GraphicEQ calibration"), this);
	addConvolutionButton = new QPushButton(tr("Create FIR + add Convolution"), this);
	addParametricButton = new QPushButton(tr("Add ParametricEQ calibration"), this);
	exportGraphicButton = new QPushButton(tr("Export GraphicEQ CSV"), this);
	exportFirButton = new QPushButton(tr("Export FIR .wav"), this);
	exportParametricButton = new QPushButton(tr("Export ParametricEQ"), this);
	statusLabel = new QLabel(tr("Choose an ASH headphone calibration."), this);
	QLabel* titleLabel = new QLabel(tr("This headphone calibration module includes local data from the ASH-Listening-Set GitHub repository created by Shanon Pearce."), this);
	titleLabel->setWordWrap(true);

	grid->addWidget(titleLabel, 0, 0, 1, 6);
	grid->addWidget(sourceComboBox, 1, 0, 1, 2);
	grid->addWidget(brandComboBox, 1, 2);
	grid->addWidget(modelComboBox, 1, 3, 1, 3);
	grid->addWidget(addGraphicButton, 2, 0, 1, 2);
	grid->addWidget(addConvolutionButton, 2, 2, 1, 2);
	grid->addWidget(addParametricButton, 2, 4, 1, 2);
	grid->addWidget(exportGraphicButton, 3, 0, 1, 2);
	grid->addWidget(exportFirButton, 3, 2, 1, 2);
	grid->addWidget(exportParametricButton, 3, 4, 1, 2);
	grid->addWidget(statusLabel, 4, 0, 1, 6);

	QRegularExpression sourceRe("Source\\s+\"([^\"]*)\"");
	QRegularExpressionMatch sourceMatch = sourceRe.match(parameters);
	if (sourceMatch.hasMatch())
		selectedSource = sourceMatch.captured(1);
	QRegularExpression brandRe("Brand\\s+\"([^\"]*)\"");
	QRegularExpressionMatch brandMatch = brandRe.match(parameters);
	if (brandMatch.hasMatch())
		selectedBrand = brandMatch.captured(1);
	QRegularExpression modelRe("Model\\s+\"([^\"]*)\"");
	QRegularExpressionMatch modelMatch = modelRe.match(parameters);
	if (modelMatch.hasMatch())
		selectedModel = modelMatch.captured(1);

	connect(sourceComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this, [this]() {
		selectedSource = sourceComboBox->currentData().toString();
		selectedBrand.clear();
		selectedModel.clear();
		refreshBrands();
		emit updateModel();
	});
	connect(brandComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this, [this]() {
		selectedBrand = brandComboBox->currentData().toString();
		selectedModel.clear();
		refreshModels();
		emit updateModel();
	});
	connect(modelComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this, [this]() {
		loadSelectedFilter();
		emit updateModel();
	});
	connect(addGraphicButton, &QPushButton::clicked, this, &HeadphoneCalibrationFilterGUI::addGraphicEQModule);
	connect(addConvolutionButton, &QPushButton::clicked, this, &HeadphoneCalibrationFilterGUI::addConvolutionModule);
	connect(addParametricButton, &QPushButton::clicked, this, &HeadphoneCalibrationFilterGUI::addParametricEQModule);
	connect(exportGraphicButton, &QPushButton::clicked, this, &HeadphoneCalibrationFilterGUI::exportGraphicEQ);
	connect(exportFirButton, &QPushButton::clicked, this, &HeadphoneCalibrationFilterGUI::exportFIR);
	connect(exportParametricButton, &QPushButton::clicked, this, &HeadphoneCalibrationFilterGUI::exportParametricEQ);

	loadAshCatalog();
	refreshSources();
}

void HeadphoneCalibrationFilterGUI::store(QString& command, QString& parameters)
{
	command = "HeadphoneCalibration";
	QStringList parts;
	if (!selectedSource.isEmpty())
		parts << QString("Source \"%1\"").arg(selectedSource);
	if (!selectedBrand.isEmpty())
		parts << QString("Brand \"%1\"").arg(selectedBrand);
	if (!selectedModel.isEmpty())
		parts << QString("Model \"%1\"").arg(selectedModel);
	parameters = parts.join(' ');
}

void HeadphoneCalibrationFilterGUI::loadAshCatalog()
{
	catalogDir = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("HeadphoneCalibrations");
	QFile file(QDir(catalogDir).absoluteFilePath("ash_hpcf_catalog.json"));
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		setStatus(tr("ASH catalog was not found."));
		return;
	}

	const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
	QVector<double> frequencies;
	for (const QJsonValue& value : root.value("frequency_axis").toArray())
		frequencies.append(value.toDouble());
	if (frequencies.isEmpty())
	{
		setStatus(tr("ASH catalog has no frequency axis."));
		return;
	}

	for (const QJsonValue& value : root.value("filters").toArray())
	{
		const QJsonObject obj = value.toObject();
		const QJsonArray gains = obj.value("mag_db").toArray();
		if (gains.size() != frequencies.size())
			continue;

		AshFilter filter;
		filter.source = obj.value("source").toString();
		filter.brand = obj.value("brand").toString();
		filter.model = obj.value("model").toString();
		filter.sample = obj.value("sample").toString();
		filter.type = obj.value("type").toString();
		if (filter.source == "ASH compilation")
		{
			filter.source = filter.brand;
			filter.brand = filter.model.section(' ', 0, 0);
		}
		for (int i = 0; i < gains.size(); ++i)
			filter.correction.append({frequencies[i], gains[i].toDouble()});
		if (!filter.source.isEmpty() && !filter.brand.isEmpty() && !filter.model.isEmpty() && !filter.correction.isEmpty())
			ashFilters.append(filter);
	}
}

void HeadphoneCalibrationFilterGUI::refreshSources()
{
	sourceComboBox->clear();
	QStringList sources;
	for (const AshFilter& filter : ashFilters)
		if (!sources.contains(filter.source, Qt::CaseInsensitive))
			sources << filter.source;
	sources.sort(Qt::CaseInsensitive);
	for (const QString& source : sources)
		sourceComboBox->addItem(source, source);

	const int row = selectedSource.isEmpty() ? -1 : sourceComboBox->findData(selectedSource);
	if (row >= 0)
		sourceComboBox->setCurrentIndex(row);
	else if (sourceComboBox->count() > 0)
		selectedSource = sourceComboBox->currentData().toString();
	refreshBrands();
}

void HeadphoneCalibrationFilterGUI::refreshBrands()
{
	brandComboBox->clear();
	QStringList brands;
	for (const AshFilter& filter : ashFilters)
		if (filter.source.compare(selectedSource, Qt::CaseInsensitive) == 0 && !brands.contains(filter.brand, Qt::CaseInsensitive))
			brands << filter.brand;
	brands.sort(Qt::CaseInsensitive);
	for (const QString& brand : brands)
		brandComboBox->addItem(brand, brand);

	const int row = selectedBrand.isEmpty() ? -1 : brandComboBox->findData(selectedBrand);
	if (row >= 0)
		brandComboBox->setCurrentIndex(row);
	else if (brandComboBox->count() > 0)
		selectedBrand = brandComboBox->currentData().toString();
	refreshModels();
}

void HeadphoneCalibrationFilterGUI::refreshModels()
{
	modelComboBox->clear();
	visibleModels.clear();
	for (int i = 0; i < ashFilters.size(); ++i)
	{
		const AshFilter& filter = ashFilters[i];
		if (filter.source.compare(selectedSource, Qt::CaseInsensitive) != 0 || filter.brand.compare(selectedBrand, Qt::CaseInsensitive) != 0)
			continue;
		const QString label = filter.sample.isEmpty() ? filter.model : filter.model + " - " + filter.sample;
		visibleModels.append(i);
		modelComboBox->addItem(label, i);
	}

	for (int row = 0; row < modelComboBox->count(); ++row)
	{
		const int index = modelComboBox->itemData(row).toInt();
		if (index >= 0 && index < ashFilters.size() && (ashFilters[index].model == selectedModel || modelComboBox->itemText(row) == selectedModel))
		{
			modelComboBox->setCurrentIndex(row);
			loadSelectedFilter();
			return;
		}
	}
	if (modelComboBox->count() > 0)
		loadSelectedFilter();
	else
	{
		ashCorrectionBands.clear();
		setStatus(tr("No ASH calibration found for this selection."));
	}
}

void HeadphoneCalibrationFilterGUI::loadSelectedFilter()
{
	const int index = modelComboBox->currentData().toInt();
	if (index < 0 || index >= ashFilters.size())
		return;

	const AshFilter& filter = ashFilters[index];
	selectedSource = filter.source;
	selectedBrand = filter.brand;
	selectedModel = modelComboBox->currentText();
	ashCorrectionBands = normalizedCorrection(filter.correction);
	setStatus(tr("Loaded ASH correction for %1.").arg(selectedModel));
}

QString HeadphoneCalibrationFilterGUI::graphicEQLine(const QVector<Band>& sourceBands) const
{
	QStringList parts;
	for (const Band& band : sourceBands)
		parts << QString("%1 %2").arg(band.frequency, 0, 'f', band.frequency < 100.0 ? 1 : 2).arg(band.gain, 0, 'f', 4);
	return "GraphicEQ: " + parts.join("; ");
}

QString HeadphoneCalibrationFilterGUI::parametricEQLine(const QVector<Band>& sourceBands) const
{
	QStringList parts;
	for (const SimplePeqBand& band : approximateCurveWithFlexPeq(sourceBands))
	{
		parts << QString("ON PEQ Fc %1 Hz Gain %2 dB Q %3")
			.arg(band.frequency, 0, 'f', band.frequency < 100.0 ? 1 : 2)
			.arg(band.gain, 0, 'f', 2)
			.arg(band.q, 0, 'f', 2);
	}
	return "ParametricEQ: " + parts.join("; ");
}

QVector<HeadphoneCalibrationFilterGUI::Band> HeadphoneCalibrationFilterGUI::normalizedCorrection(QVector<Band> result) const
{
	double peakGain = -999.0;
	for (const Band& band : result)
		peakGain = std::max(peakGain, band.gain);
	if (peakGain > -900.0)
		for (Band& band : result)
			band.gain = std::clamp(band.gain - peakGain, -24.0, 0.0);
	return result;
}

void HeadphoneCalibrationFilterGUI::addGraphicEQModule()
{
	if (ashCorrectionBands.isEmpty() || filterTable == nullptr)
		return;
	filterTable->addLine(graphicEQLine(ashCorrectionBands));
	filterTable->updateGuis();
	setStatus(tr("Added ASH GraphicEQ calibration."));
}

void HeadphoneCalibrationFilterGUI::addConvolutionModule()
{
	if (ashCorrectionBands.isEmpty() || filterTable == nullptr)
		return;
	const QString path = QFileDialog::getSaveFileName(this, tr("Create ASH FIR for current sample rate"), QString("ASH_Calibration_FIR_%1Hz.wav").arg(currentDeviceSampleRate()), tr("WAV files (*.wav)"));
	if (path.isEmpty() || !exportFIRToPath(ashCorrectionBands, path))
		return;
	filterTable->addLine("Convolution: " + path);
	filterTable->updateGuis();
	setStatus(tr("Added ASH FIR at %1 Hz.").arg(currentDeviceSampleRate()));
}

void HeadphoneCalibrationFilterGUI::addParametricEQModule()
{
	if (ashCorrectionBands.isEmpty() || filterTable == nullptr)
		return;
	filterTable->addLine(parametricEQLine(ashCorrectionBands));
	filterTable->updateGuis();
	setStatus(tr("Added ASH ParametricEQ calibration."));
}

void HeadphoneCalibrationFilterGUI::exportGraphicEQ()
{
	if (ashCorrectionBands.isEmpty())
		return;
	const QString path = QFileDialog::getSaveFileName(this, tr("Export ASH GraphicEQ CSV"), "ASH-GraphicEQ.csv", tr("CSV files (*.csv);;Text files (*.txt);;All files (*.*)"));
	if (path.isEmpty())
		return;
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return;
	QTextStream stream(&file);
	stream << "Frequency,Gain\n";
	for (const Band& band : ashCorrectionBands)
		stream << QString::number(band.frequency, 'f', 2) << "," << QString::number(band.gain, 'f', 4) << "\n";
	file.commit();
	setStatus(tr("Exported ASH GraphicEQ CSV."));
}

void HeadphoneCalibrationFilterGUI::exportParametricEQ()
{
	if (ashCorrectionBands.isEmpty())
		return;
	const QString path = QFileDialog::getSaveFileName(this, tr("Export ASH ParametricEQ"), "ASH-ParametricEQ.txt", tr("Text files (*.txt);;All files (*.*)"));
	if (path.isEmpty())
		return;
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return;
	QTextStream stream(&file);
	stream << parametricEQLine(ashCorrectionBands) << "\n";
	file.commit();
	setStatus(tr("Exported ASH ParametricEQ."));
}

void HeadphoneCalibrationFilterGUI::exportFIR()
{
	if (ashCorrectionBands.isEmpty())
		return;
	const QString path = QFileDialog::getSaveFileName(this, tr("Export ASH FIR for current sample rate"), QString("ASH_Calibration_FIR_%1Hz.wav").arg(currentDeviceSampleRate()), tr("WAV files (*.wav)"));
	if (!path.isEmpty())
		exportFIRToPath(ashCorrectionBands, path);
}

bool HeadphoneCalibrationFilterGUI::exportFIRToPath(const QVector<Band>& sourceBands, const QString& path)
{
	std::vector<FilterNode> nodes;
	for (const Band& band : sourceBands)
		nodes.push_back(FilterNode(band.frequency, band.gain));
	std::vector<double> impulse = GraphicEQFilter::createImpulseResponse(nodes, 16384, static_cast<float>(currentDeviceSampleRate()));
	if (impulse.empty())
	{
		QMessageBox::warning(this, tr("Export FIR"), tr("Could not generate FIR impulse response."));
		return false;
	}

	SF_INFO info = {};
	info.channels = 1;
	info.samplerate = static_cast<int>(currentDeviceSampleRate());
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
	SNDFILE* file = sf_open(path.toStdString().c_str(), SFM_WRITE, &info);
	if (file == nullptr)
	{
		QMessageBox::warning(this, tr("Export FIR"), tr("Could not create FIR file."));
		return false;
	}
	sf_write_double(file, impulse.data(), static_cast<sf_count_t>(impulse.size()));
	sf_close(file);
	setStatus(tr("Exported ASH FIR at %1 Hz.").arg(currentDeviceSampleRate()));
	return true;
}

unsigned HeadphoneCalibrationFilterGUI::currentDeviceSampleRate() const
{
	if (filterTable != nullptr && filterTable->getSelectedDevice() != nullptr && filterTable->getSelectedDevice()->getSampleRate() != 0)
		return filterTable->getSelectedDevice()->getSampleRate();
	return 48000;
}

void HeadphoneCalibrationFilterGUI::setStatus(const QString& text)
{
	statusLabel->setText(text);
}

void HeadphoneCalibrationFilterGUIFactory::initialize(FilterTable* filterTable)
{
	this->filterTable = filterTable;
}

QList<FilterTemplate> HeadphoneCalibrationFilterGUIFactory::createFilterTemplates()
{
	return QList<FilterTemplate>() << FilterTemplate(tr("Headphone calibration"), "HeadphoneCalibration: ", QStringList(tr("Advanced filters")));
}

IFilterGUI* HeadphoneCalibrationFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	return command == "HeadphoneCalibration" ? new HeadphoneCalibrationFilterGUI(parameters, filterTable) : nullptr;
}
