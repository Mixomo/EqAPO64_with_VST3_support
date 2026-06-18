#include <QGridLayout>
#include <QDialog>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
#include <fftw3.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "filters/VUMeterProtocol.h"
#include "AudioToolFilterGUIFactory.h"

using namespace std;

class ResettableSlider : public QSlider
{
public:
	explicit ResettableSlider(Qt::Orientation orientation, QWidget* parent = nullptr)
		: QSlider(orientation, parent)
	{
	}

	std::function<void()> resetHandler;

protected:
	void mouseDoubleClickEvent(QMouseEvent* event) override
	{
		if (resetHandler)
		{
			resetHandler();
			event->accept();
			return;
		}
		QSlider::mouseDoubleClickEvent(event);
	}
};

class ResettableDoubleSpinBox : public QDoubleSpinBox
{
public:
	explicit ResettableDoubleSpinBox(QWidget* parent = nullptr)
		: QDoubleSpinBox(parent)
	{
	}

	std::function<void()> resetHandler;

protected:
	void mouseDoubleClickEvent(QMouseEvent* event) override
	{
		if (resetHandler)
		{
			resetHandler();
			event->accept();
			return;
		}
		QDoubleSpinBox::mouseDoubleClickEvent(event);
	}
};

static QString tokenValue(const QString& parameters, const QString& key, const QString& defaultValue)
{
	QStringList parts;
	QRegularExpression re("\"([^\"]*)\"|(\\S+)");
	auto it = re.globalMatch(parameters);
	while (it.hasNext())
	{
		const QRegularExpressionMatch match = it.next();
		parts << (match.captured(1).isNull() ? match.captured(2) : match.captured(1));
	}
	for (int i = 0; i + 1 < parts.size(); ++i)
		if (parts[i].compare(key, Qt::CaseInsensitive) == 0)
			return parts[i + 1];
	return defaultValue;
}

static double tokenDouble(const QString& parameters, const QString& key, double defaultValue)
{
	bool ok = false;
	double value = tokenValue(parameters, key, QString()).toDouble(&ok);
	return ok ? value : defaultValue;
}

static double dbFromLinear(double value)
{
	return value > 0.000000000001 ? 20.0 * log10(value) : -90.0;
}

class VUMeterPanel : public QWidget
{
public:
	explicit VUMeterPanel(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setMinimumSize(620, 460);
		timer.setInterval(33);
		connect(&timer, &QTimer::timeout, this, [this]() {
			readSharedData();
			update();
		});
		timer.start();
	}

	~VUMeterPanel() override
	{
		disconnectMeter();
	}

	QSize sizeHint() const override
	{
		return QSize(contentWidth(), 560);
	}

	void setMeterId(const QString& value)
	{
		QString normalized = value.trimmed().isEmpty() ? "default" : value.trimmed();
		if (meterId == normalized)
			return;
		meterId = normalized;
		disconnectMeter();
	}

	void reset()
	{
		if (connectMeter())
			shared->resetRequest++;
	}

	bool hasValidData() const { return valid; }

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.fillRect(rect(), QColor(13, 15, 18));
		p.setRenderHint(QPainter::Antialiasing, true);

		const QRect inner = rect().adjusted(16, 14, -16, -14);
		p.setPen(QColor(225, 230, 235));
		QFont titleFont = p.font();
		titleFont.setBold(true);
		titleFont.setPointSize(titleFont.pointSize() + 1);
		p.setFont(titleFont);
		p.drawText(inner.left(), inner.top() + 16, tr("APO Loudness / VU Meter"));
		if (!valid)
		{
			p.setPen(QColor(180, 185, 190));
			p.drawText(inner.adjusted(0, 56, 0, 0), tr("Waiting for VU meter data"));
			return;
		}

		const int top = inner.top() + 64;
		const int bottom = inner.bottom() - 54;
		const int meterHeight = bottom - top;
		const int channelCount = max(1, static_cast<int>((std::min)(data.channelCount, static_cast<std::uint32_t>(VUMETER_MAX_CHANNELS))));
		const int leftScale = 48;
		const int barWidth = 52;
		const int pairGap = 18;
		const int channelGap = 56;
		const int meterWidth = channelCount * (2 * barWidth + pairGap + channelGap);

		p.setPen(QColor(85, 90, 98));
		for (int db = 6; db >= -60; db -= 6)
		{
			const int y = bottom - static_cast<int>((db + 60) / 66.0 * meterHeight);
			p.drawLine(inner.left() + leftScale - 6, y, inner.left() + leftScale + meterWidth - channelGap - 4, y);
			if (db % 12 == 0 || db == 6)
			{
				p.setPen(QColor(160, 166, 174));
				p.drawText(QRect(inner.left(), y - 8, leftScale - 10, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(db));
				p.setPen(QColor(85, 90, 98));
			}
		}

		int x = inner.left() + leftScale;
		for (int ch = 0; ch < channelCount; ++ch)
		{
			drawBar(p, QRect(x, top, barWidth, meterHeight), dbFromLinear(data.rms[ch]), ch, false);
			drawBar(p, QRect(x + barWidth + pairGap, top, barWidth, meterHeight), dbFromLinear(data.peak[ch]), ch, true);
			p.setPen(QColor(210, 215, 220));
			p.drawText(QRect(x - 4, bottom + 6, barWidth * 2 + pairGap + 8, 16), Qt::AlignCenter, channelLabel(ch));
			p.setPen(QColor(120, 128, 136));
			p.drawText(QRect(x - 4, bottom + 22, barWidth * 2 + pairGap + 8, 16), Qt::AlignCenter, "RMS  PK");
			x += 2 * barWidth + pairGap + channelGap;
		}
	}

private:
	QString channelLabel(int ch) const
	{
		static const char* labels[] = {"L", "R", "C", "LFE", "RL", "RR", "SL", "SR"};
		if (ch >= 0 && ch < 8)
			return labels[ch];
		return QString::number(ch + 1);
	}

	void drawBar(QPainter& p, const QRect& bar, double db, int channel, bool peak)
	{
		p.setPen(QColor(65, 70, 76));
		p.setBrush(QColor(28, 31, 36));
		p.drawRoundedRect(bar, 2, 2);

		const double clamped = (std::max)(-60.0, (std::min)(6.0, db));
		const int fillHeight = static_cast<int>((clamped + 60.0) / 66.0 * bar.height());
		QRect fill = bar.adjusted(2, bar.height() - fillHeight + 1, -2, -2);
		QLinearGradient gradient(fill.topLeft(), fill.bottomLeft());
		gradient.setColorAt(0.0, QColor(250, 60, 60));
		gradient.setColorAt(0.18, QColor(245, 220, 55));
		gradient.setColorAt(0.38, QColor(90, 230, 90));
		gradient.setColorAt(1.0, QColor(70, 210, 190));
		p.fillRect(fill, gradient);

		if (peak)
		{
			const int y = bar.bottom() - static_cast<int>(((std::max)(-60.0, (std::min)(6.0, dbFromLinear(data.peakHold[channel]))) + 60.0) / 66.0 * bar.height());
			p.setPen(QColor(255, 245, 90));
			p.drawLine(bar.left() + 2, y, bar.right() - 2, y);
		}

		p.setPen(QColor(215, 220, 226));
		p.drawText(QRect(bar.left() - 10, bar.top() - 18, bar.width() + 20, 14), Qt::AlignCenter, QString("%1").arg(db, 0, 'f', 1));
	}

	bool connectMeter()
	{
		if (shared != nullptr)
			return true;
		const QString objectName = QStringLiteral("Global\\EqAPO_VUMeter_") + meterId;
		mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, reinterpret_cast<const wchar_t*>(objectName.utf16()));
		if (mapping == NULL)
			return false;
		shared = static_cast<VUMeterSharedData*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VUMeterSharedData)));
		if (shared == nullptr)
		{
			CloseHandle(mapping);
			mapping = NULL;
			return false;
		}
		return true;
	}

	void disconnectMeter()
	{
		if (shared != nullptr)
		{
			UnmapViewOfFile(shared);
			shared = nullptr;
		}
		if (mapping != NULL)
		{
			CloseHandle(mapping);
			mapping = NULL;
		}
		valid = false;
	}

	void readSharedData()
	{
		valid = false;
		if (!connectMeter())
			return;
		if (shared->magic != VUMETER_MAGIC || shared->version != VUMETER_VERSION)
		{
			disconnectMeter();
			return;
		}
		memcpy(&data, shared, sizeof(data));
		const int width = contentWidth();
		if (minimumWidth() != width)
		{
			setMinimumWidth(width);
			updateGeometry();
		}
		valid = true;
	}

	int contentWidth() const
	{
		const int channels = max(1, static_cast<int>((std::min)(data.channelCount, static_cast<std::uint32_t>(VUMETER_MAX_CHANNELS))));
		const int margins = 32;
		const int leftScale = 48;
		const int barWidth = 52;
		const int pairGap = 18;
		const int channelGap = 56;
		return (std::max)(620, margins + leftScale + channels * (2 * barWidth + pairGap + channelGap));
	}

	QString meterId = "default";
	QTimer timer;
	HANDLE mapping = NULL;
	VUMeterSharedData* shared = nullptr;
	VUMeterSharedData data = {};
	bool valid = false;
};

class VUMeterStatsPanel : public QWidget
{
public:
	explicit VUMeterStatsPanel(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setMinimumWidth(contentWidth());
		timer.setInterval(33);
		connect(&timer, &QTimer::timeout, this, [this]() {
			readSharedData();
			update();
		});
		timer.start();
	}

	~VUMeterStatsPanel() override
	{
		disconnectMeter();
	}

	QSize sizeHint() const override
	{
		return QSize(contentWidth(), contentHeight());
	}

	void setMeterId(const QString& value)
	{
		QString normalized = value.trimmed().isEmpty() ? "default" : value.trimmed();
		if (meterId == normalized)
			return;
		meterId = normalized;
		disconnectMeter();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.fillRect(rect(), QColor(13, 15, 18));
		p.setRenderHint(QPainter::Antialiasing, true);
		const QRect panel = QRect(4, 4, contentWidth() - 12, contentHeight() - 8);
		p.setPen(QColor(60, 64, 70));
		p.setBrush(QColor(22, 25, 29));
		p.drawRoundedRect(panel, 4, 4);
		if (!valid)
		{
			p.setPen(QColor(145, 152, 162));
			p.drawText(panel.adjusted(12, 14, -12, -14), Qt::AlignLeft | Qt::TextWordWrap, tr("Waiting for meter data"));
			return;
		}
		double maxPeak = 0.0;
		double maxRms = 0.0;
		qulonglong clips = 0;
		const int channelCount = max(1, static_cast<int>((std::min)(data.channelCount, static_cast<std::uint32_t>(VUMETER_MAX_CHANNELS))));
		for (int ch = 0; ch < channelCount; ++ch)
		{
			maxPeak = max(maxPeak, data.peak[ch]);
			maxRms = max(maxRms, data.rms[ch]);
			clips += data.clip[ch];
		}
		drawMetric(p, panel, 18, "Momentary", QString("%1 LUFS").arg(data.lufsMomentary, 0, 'f', 1), QColor(84, 205, 255));
		drawMetric(p, panel, 68, "Short-term", QString("%1 LUFS").arg(data.lufsShortTerm, 0, 'f', 1), QColor(111, 232, 124));
		drawMetric(p, panel, 118, "Integrated", QString("%1 LUFS").arg(data.lufsIntegrated, 0, 'f', 1), QColor(250, 220, 80));
		drawMetric(p, panel, 168, "True peak*", QString("%1 dBFS").arg(dbFromLinear(maxPeak), 0, 'f', 1), QColor(255, 128, 96));
		drawMetric(p, panel, 218, "Max RMS", QString("%1 dBFS").arg(dbFromLinear(maxRms), 0, 'f', 1), QColor(180, 190, 205));
		drawMetric(p, panel, 268, "Clips", QString::number(clips), clips ? QColor(255, 80, 80) : QColor(180, 190, 205));
		p.setPen(QColor(225, 230, 235));
		QFont header = p.font();
		header.setBold(true);
		p.setFont(header);
		p.drawText(QRect(panel.left() + 14, panel.top() + 328, panel.width() - 28, 24), Qt::AlignLeft | Qt::AlignVCenter, tr("Detailed readout"));
		header.setBold(false);
		p.setFont(header);
		drawGlobalDetail(p, panel, 360, maxPeak, maxRms, clips);
		for (int ch = 0; ch < channelCount; ++ch)
			drawChannelMetric(p, panel, 456 + ch * 96, ch);
	}

private:
	int contentWidth() const
	{
		return 560;
	}

	int contentHeight() const
	{
		const int channelCount = max(1, static_cast<int>((std::min)(data.channelCount, static_cast<std::uint32_t>(VUMETER_MAX_CHANNELS))));
		return (std::max)(560, 560 + channelCount * 96);
	}

	QString channelLabel(int ch) const
	{
		static const char* labels[] = {"L", "R", "C", "LFE", "RL", "RR", "SL", "SR"};
		if (ch >= 0 && ch < 8)
			return labels[ch];
		return QString::number(ch + 1);
	}

	void drawMetric(QPainter& p, const QRect& panel, int y, const QString& name, const QString& value, const QColor& color)
	{
		const QRect row(panel.left() + 14, panel.top() + y, panel.width() - 28, 40);
		p.setPen(QColor(145, 152, 162));
		p.drawText(row, Qt::AlignLeft | Qt::AlignTop, name);
		QFont f = p.font();
		f.setBold(true);
		f.setPointSize(f.pointSize() + 2);
		p.setFont(f);
		p.setPen(color);
		p.drawText(row, Qt::AlignLeft | Qt::AlignBottom, value);
		f.setBold(false);
		f.setPointSize(f.pointSize() - 2);
		p.setFont(f);
	}

	void drawGlobalDetail(QPainter& p, const QRect& panel, int y, double maxPeak, double maxRms, qulonglong clips)
	{
		const QRect row(panel.left() + 14, panel.top() + y, panel.width() - 28, 82);
		p.setPen(QColor(70, 76, 84));
		p.drawLine(row.left(), row.bottom(), row.right(), row.bottom());
		p.setPen(QColor(210, 216, 224));
		QFont labelFont = p.font();
		labelFont.setBold(true);
		p.setFont(labelFont);
		p.drawText(QRect(row.left(), row.top(), 70, 22), Qt::AlignLeft | Qt::AlignVCenter, tr("Global"));
		labelFont.setBold(false);
		p.setFont(labelFont);
		p.setPen(QColor(145, 152, 162));
		const QString line1 = QString("M %1 LUFS  S %2 LUFS  I %3 LUFS")
			.arg(data.lufsMomentary, 0, 'f', 1)
			.arg(data.lufsShortTerm, 0, 'f', 1)
			.arg(data.lufsIntegrated, 0, 'f', 1);
		const QString line2 = QString("Peak %1 dBFS  RMS %2 dBFS  Clips %3")
			.arg(dbFromLinear(maxPeak), 0, 'f', 1)
			.arg(dbFromLinear(maxRms), 0, 'f', 1)
			.arg(clips);
		p.drawText(QRect(row.left(), row.top() + 28, row.width(), 20), Qt::AlignLeft | Qt::AlignVCenter, line1);
		p.drawText(QRect(row.left(), row.top() + 54, row.width(), 20), Qt::AlignLeft | Qt::AlignVCenter, line2);
	}

	QString lufsText(double value) const
	{
		return std::isfinite(value) ? QString("%1 LUFS").arg(value, 0, 'f', 1) : QString("-inf LUFS");
	}

	void drawChannelMetric(QPainter& p, const QRect& panel, int y, int channel)
	{
		const QRect row(panel.left() + 14, panel.top() + y, panel.width() - 28, 86);
		p.setPen(QColor(70, 76, 84));
		p.drawLine(row.left(), row.bottom(), row.right(), row.bottom());
		p.setPen(QColor(210, 216, 224));
		QFont labelFont = p.font();
		labelFont.setBold(true);
		p.setFont(labelFont);
		p.drawText(QRect(row.left(), row.top(), 72, 24), Qt::AlignLeft | Qt::AlignVCenter, channelLabel(channel));
		labelFont.setBold(false);
		p.setFont(labelFont);
		p.setPen(QColor(145, 152, 162));
		p.drawText(QRect(row.left(), row.top() + 24, row.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
			QString("M %1  S %2").arg(lufsText(data.channelLufsMomentary[channel]), lufsText(data.channelLufsShortTerm[channel])));
		p.drawText(QRect(row.left(), row.top() + 44, row.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
			QString("I %1  Max RMS %2 dBFS").arg(lufsText(data.channelLufsIntegrated[channel])).arg(dbFromLinear(data.rms[channel]), 0, 'f', 1));
		p.drawText(QRect(row.left(), row.top() + 64, row.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
			QString("True peak* %1 dBFS  Hold %2 dBFS  Clips %3")
				.arg(dbFromLinear(data.peak[channel]), 0, 'f', 1)
				.arg(dbFromLinear(data.peakHold[channel]), 0, 'f', 1)
				.arg(data.clip[channel]));
	}

	bool connectMeter()
	{
		if (shared != nullptr)
			return true;
		const QString objectName = QStringLiteral("Global\\EqAPO_VUMeter_") + meterId;
		mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, reinterpret_cast<const wchar_t*>(objectName.utf16()));
		if (mapping == NULL)
			return false;
		shared = static_cast<VUMeterSharedData*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VUMeterSharedData)));
		if (shared == nullptr)
		{
			CloseHandle(mapping);
			mapping = NULL;
			return false;
		}
		return true;
	}

	void disconnectMeter()
	{
		if (shared != nullptr)
		{
			UnmapViewOfFile(shared);
			shared = nullptr;
		}
		if (mapping != NULL)
		{
			CloseHandle(mapping);
			mapping = NULL;
		}
		valid = false;
	}

	void readSharedData()
	{
		valid = false;
		if (!connectMeter())
			return;
		if (shared->magic != VUMETER_MAGIC || shared->version != VUMETER_VERSION)
		{
			disconnectMeter();
			return;
		}
		memcpy(&data, shared, sizeof(data));
		setMinimumSize(contentWidth(), contentHeight());
		resize(contentWidth(), contentHeight());
		updateGeometry();
		valid = true;
	}

	QString meterId = "default";
	QTimer timer;
	HANDLE mapping = NULL;
	VUMeterSharedData* shared = nullptr;
	VUMeterSharedData data = {};
	bool valid = false;
};

QWidget* AudioToolFilterGUI::addSliderControl(QGridLayout* grid, const QString& label, QDoubleSpinBox** spin, double min, double max, double value, const QString& suffix, int row, int column, int decimals)
{
	QWidget* box = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(box);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);
	QLabel* title = new QLabel(label, box);
	ResettableSlider* slider = new ResettableSlider(Qt::Horizontal, box);
	ResettableDoubleSpinBox* doubleSpin = new ResettableDoubleSpinBox(box);
	*spin = doubleSpin;
	(*spin)->setRange(min, max);
	(*spin)->setDecimals(decimals);
	(*spin)->setSuffix(suffix);
	(*spin)->setValue(value);
	(*spin)->setKeyboardTracking(false);
	(*spin)->setProperty("defaultValue", value);
	slider->setRange(0, 1000);
	const int defaultSliderValue = static_cast<int>((value - min) / (max - min) * 1000.0);
	slider->setValue(defaultSliderValue);
	slider->setProperty("defaultValue", defaultSliderValue);
	layout->addWidget(title);
	layout->addWidget(slider);
	layout->addWidget(*spin);
	auto resetOneControl = [this, spin, slider, min, max, value]() {
		QSignalBlocker spinBlocker(*spin);
		QSignalBlocker sliderBlocker(slider);
		(*spin)->setValue(value);
		slider->setValue(static_cast<int>((value - min) / (max - min) * 1000.0));
		emit updateModel();
	};
	slider->resetHandler = resetOneControl;
	doubleSpin->resetHandler = resetOneControl;
	connect(slider, &QSlider::valueChanged, this, [this, spin, min, max](int sliderValue) {
		const double value = min + (max - min) * sliderValue / 1000.0;
		(*spin)->setValue(value);
	});
	connect(*spin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [this, slider, min, max](double value) {
		QSignalBlocker blocker(slider);
		slider->setValue(static_cast<int>((value - min) / (max - min) * 1000.0));
		emit updateModel();
	});
	grid->addWidget(box, row, column);
	return box;
}

QPushButton* AudioToolFilterGUI::addModuleResetButton(QGridLayout* grid, int row, int column, int columnSpan)
{
	QPushButton* button = new QPushButton(tr("Reset module"), this);
	grid->addWidget(button, row, column, 1, columnSpan);
	connect(button, &QPushButton::clicked, this, [this]() { resetModuleToDefaults(); });
	return button;
}

void AudioToolFilterGUI::addChannelSelector(QGridLayout* grid, const QString& parameters, int row, int column, int columnSpan)
{
	QWidget* box = new QWidget(this);
	QHBoxLayout* layout = new QHBoxLayout(box);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	layout->addWidget(new QLabel(tr("Channels"), box));
	const QStringList names = QStringList() << "L" << "R" << "C" << "LFE" << "RL" << "RR" << "SL" << "SR";
	const QString channels = tokenValue(parameters, "Channels", "all").toUpper();
	const QStringList listedChannels = channels.split(',', Qt::SkipEmptyParts);
	for (const QString& name : names)
	{
		QCheckBox* check = new QCheckBox(name, box);
		check->setChecked(channels == "ALL"
			|| (channels == "STEREO" && (name == "L" || name == "R"))
			|| (channels == "MONO" && name == "L")
			|| listedChannels.contains(name));
		channelChecks.append(check);
		layout->addWidget(check);
		connect(check, &QCheckBox::toggled, this, [this](bool) { emit updateModel(); });
	}
	layout->addStretch(1);
	grid->addWidget(box, row, column, 1, columnSpan);
}

QString AudioToolFilterGUI::selectedChannels() const
{
	QStringList selected;
	for (QCheckBox* check : channelChecks)
		if (check->isChecked())
			selected << check->text();
	if (selected.size() == channelChecks.size())
		return "all";
	const QStringList mono = QStringList() << "L";
	const QStringList stereo = QStringList() << "L" << "R";
	if (selected == mono)
		return "mono";
	if (selected == stereo)
		return "stereo";
	return selected.isEmpty() ? "all" : selected.join(",");
}

void AudioToolFilterGUI::resetModuleToDefaults()
{
	auto setSpin = [](QDoubleSpinBox* spin, double value) {
		if (spin != nullptr)
			spin->setValue(value);
	};
	auto setCombo = [](QComboBox* combo, const QString& value) {
		if (combo != nullptr)
			combo->setCurrentText(value);
	};
	if (commandName == "ToneGenerator")
	{
		if (stateButton != nullptr)
			stateButton->setChecked(false);
		setCombo(typeComboBox, "Sine");
		setCombo(modeComboBox, "Replace");
		setSpin(frequencySpinBox, 1000);
		setSpin(levelSpinBox, -20);
		setSpin(startSpinBox, 20);
		setSpin(endSpinBox, 20000);
		setSpin(durationSpinBox, 10);
		for (QCheckBox* check : channelChecks)
			check->setChecked(true);
	}
	else if (commandName == "Pan")
	{
		setSpin(positionSpinBox, 0);
		setSpin(widthSpinBox, 100);
	}
	else if (commandName == "Crossfeed")
	{
		setCombo(crossfeedAlgorithmComboBox, "Natural");
		setCombo(crossfeedPresetComboBox, "Average Male");
		setSpin(amountSpinBox, 35);
		setSpin(headCircumferenceSpinBox, 57);
		setSpin(headWidthSpinBox, 15.0);
		setSpin(headLengthSpinBox, 19.0);
		setSpin(angleSpinBox, 60);
		setSpin(cutoffSpinBox, 900);
		setSpin(directSpinBox, 100);
	}
	else if (commandName == "Chorus")
	{
		setSpin(rateSpinBox, 0.4);
		setSpin(depthSpinBox, 8);
		setSpin(mixSpinBox, 25);
		setSpin(feedbackSpinBox, 0);
	}
	else if (commandName == "Reverb")
	{
		setSpin(roomSpinBox, 50);
		setSpin(dampingSpinBox, 50);
		setSpin(wetSpinBox, 20);
		setSpin(drySpinBox, 100);
		setSpin(widthSpinBox, 100);
	}
	else if (commandName == "VUMeter")
	{
		setCombo(rmsStandardComboBox, "AES17");
		setCombo(lufsStandardComboBox, "ITU-R BS.1770-5");
		for (QCheckBox* check : channelChecks)
			check->setChecked(true);
		if (meterPanel != nullptr)
			meterPanel->reset();
	}
	updateMeterPanel();
	emit updateModel();
}

AudioToolFilterGUI::AudioToolFilterGUI(const QString& command, const QString& parameters)
	: commandName(command)
{
	QGridLayout* grid = new QGridLayout(this);
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setHorizontalSpacing(8);
	grid->setVerticalSpacing(6);

	if (commandName == "ToneGenerator")
	{
		stateButton = new QPushButton(tokenDouble(parameters, "State", 0) != 0 ? tr("Stop") : tr("Play"), this);
		stateButton->setCheckable(true);
		stateButton->setChecked(tokenDouble(parameters, "State", 0) != 0);
		typeComboBox = new QComboBox(this);
		typeComboBox->addItems(QStringList() << "Sine" << "White" << "Pink" << "Brown" << "Sweep");
		typeComboBox->setCurrentText(tokenValue(parameters, "Type", "Sine"));
		modeComboBox = new QComboBox(this);
		modeComboBox->addItems(QStringList() << "Replace" << "Mix");
		modeComboBox->setCurrentText(tokenValue(parameters, "Mode", "Replace"));
		grid->addWidget(stateButton, 0, 0);
		grid->addWidget(typeComboBox, 0, 1);
		grid->addWidget(modeComboBox, 0, 2);
		addModuleResetButton(grid, 0, 3);
		addSliderControl(grid, tr("Frequency"), &frequencySpinBox, 20, 20000, tokenDouble(parameters, "Frequency", 1000), " Hz", 1, 0, 1);
		addSliderControl(grid, tr("Level"), &levelSpinBox, -80, 0, tokenDouble(parameters, "Level", -20), " dB", 1, 1, 1);
		addSliderControl(grid, tr("Sweep start"), &startSpinBox, 20, 20000, tokenDouble(parameters, "Start", 20), " Hz", 1, 2, 1);
		addSliderControl(grid, tr("Sweep end"), &endSpinBox, 20, 20000, tokenDouble(parameters, "End", 20000), " Hz", 1, 3, 1);
		addSliderControl(grid, tr("Duration"), &durationSpinBox, 1, 120, tokenDouble(parameters, "Duration", 10), " s", 1, 4, 1);
		addChannelSelector(grid, parameters, 2, 0, 5);
		connect(stateButton, &QPushButton::toggled, this, [this](bool checked) {
			stateButton->setText(checked ? tr("Stop") : tr("Play"));
			emit updateModel();
		});
	}
	else if (commandName == "Pan")
	{
		addSliderControl(grid, tr("Position"), &positionSpinBox, -100, 100, tokenDouble(parameters, "Position", 0), QString(), 0, 0, 1);
		addSliderControl(grid, tr("Width"), &widthSpinBox, 0, 200, tokenDouble(parameters, "Width", 100), " %", 0, 1, 1);
		addModuleResetButton(grid, 0, 2);
	}
	else if (commandName == "Crossfeed")
	{
		crossfeedAlgorithmComboBox = new QComboBox(this);
		crossfeedAlgorithmComboBox->addItems(QStringList() << "Natural" << "BS2B");
		QString algorithm = tokenValue(parameters, "Algorithm", "Natural");
		if (algorithm.compare("Spatial", Qt::CaseInsensitive) == 0)
			algorithm = "Natural";
		crossfeedAlgorithmComboBox->setCurrentText(algorithm);
		crossfeedPresetComboBox = new QComboBox(this);
		crossfeedPresetComboBox->addItems(QStringList() << "Average Female" << "Average Male");
		QString preset = tokenValue(parameters, "Preset", "Average Male");
		if (preset.compare("Average adult", Qt::CaseInsensitive) == 0 || preset.compare("B&K 5128", Qt::CaseInsensitive) == 0 || preset.compare("Large head", Qt::CaseInsensitive) == 0)
			preset = "Average Male";
		else if (preset.compare("Small head", Qt::CaseInsensitive) == 0 || preset.compare("Custom", Qt::CaseInsensitive) == 0)
			preset = "Average Female";
		crossfeedPresetComboBox->setCurrentText(preset);
		grid->addWidget(new QLabel(tr("Algorithm"), this), 0, 0);
		grid->addWidget(crossfeedAlgorithmComboBox, 0, 1);
		grid->addWidget(new QLabel(tr("Anatomy"), this), 0, 2);
		grid->addWidget(crossfeedPresetComboBox, 0, 3);
		addModuleResetButton(grid, 0, 4);
		addSliderControl(grid, tr("Amount"), &amountSpinBox, 0, 100, tokenDouble(parameters, "Amount", 35), " %", 1, 0, 1);
		addSliderControl(grid, tr("Circumference"), &headCircumferenceSpinBox, 45, 70, tokenDouble(parameters, "Circumference", preset == "Average Female" ? 55 : 57), " cm", 1, 1, 1);
		addSliderControl(grid, tr("Head width"), &headWidthSpinBox, 10, 22, tokenDouble(parameters, "HeadWidth", tokenDouble(parameters, "Width", preset == "Average Female" ? 14.0 : 15.0)), " cm", 1, 2, 1);
		addSliderControl(grid, tr("Head length"), &headLengthSpinBox, 14, 25, tokenDouble(parameters, "HeadLength", tokenDouble(parameters, "Length", preset == "Average Female" ? 17.8 : 19.0)), " cm", 1, 3, 1);
		addSliderControl(grid, tr("Angle"), &angleSpinBox, 10, 90, tokenDouble(parameters, "Angle", 60), " deg", 2, 0, 1);
		addSliderControl(grid, tr("Cutoff"), &cutoffSpinBox, 200, 2500, tokenDouble(parameters, "Cutoff", 900), " Hz", 2, 1, 0);
		addSliderControl(grid, tr("Direct"), &directSpinBox, 50, 120, tokenDouble(parameters, "Direct", 100), " %", 2, 2, 1);
		connect(crossfeedPresetComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int) {
			const QString preset = crossfeedPresetComboBox->currentText();
			if (preset == "Average Female")
			{
				headCircumferenceSpinBox->setValue(55.0);
				headWidthSpinBox->setValue(14.0);
				headLengthSpinBox->setValue(17.8);
			}
			else
			{
				headCircumferenceSpinBox->setValue(57.0);
				headWidthSpinBox->setValue(15.0);
				headLengthSpinBox->setValue(19.0);
			}
		});
	}
	else if (commandName == "Chorus")
	{
		addSliderControl(grid, tr("Rate"), &rateSpinBox, 0.05, 5, tokenDouble(parameters, "Rate", 0.4), " Hz", 0, 0, 2);
		addSliderControl(grid, tr("Depth"), &depthSpinBox, 0, 30, tokenDouble(parameters, "Depth", 8), " ms", 0, 1, 1);
		addSliderControl(grid, tr("Mix"), &mixSpinBox, 0, 100, tokenDouble(parameters, "Mix", 25), " %", 0, 2, 1);
		addSliderControl(grid, tr("Feedback"), &feedbackSpinBox, -80, 80, tokenDouble(parameters, "Feedback", 0), " %", 0, 3, 1);
		addModuleResetButton(grid, 0, 4);
	}
	else if (commandName == "Reverb")
	{
		addSliderControl(grid, tr("Room"), &roomSpinBox, 0, 100, tokenDouble(parameters, "RoomSize", 50), " %", 0, 0, 1);
		addSliderControl(grid, tr("Damping"), &dampingSpinBox, 0, 100, tokenDouble(parameters, "Damping", 50), " %", 0, 1, 1);
		addSliderControl(grid, tr("Wet"), &wetSpinBox, 0, 100, tokenDouble(parameters, "Wet", 20), " %", 0, 2, 1);
		addSliderControl(grid, tr("Dry"), &drySpinBox, 0, 150, tokenDouble(parameters, "Dry", 100), " %", 0, 3, 1);
		addSliderControl(grid, tr("Width"), &widthSpinBox, 0, 100, tokenDouble(parameters, "Width", 100), " %", 0, 4, 1);
		addModuleResetButton(grid, 0, 5);
	}
	else if (commandName == "VUMeter")
	{
		panelButton = new QPushButton(tr("Open panel"), this);
		panelButton->setCheckable(true);
		resetButton = new QPushButton(tr("Reset"), this);
		rmsStandardComboBox = new QComboBox(this);
		rmsStandardComboBox->addItems(QStringList() << "AES17" << "IEC 61606");
		rmsStandardComboBox->setCurrentText(tokenValue(parameters, "RMS", tokenValue(parameters, "RMSStandard", "AES17")));
		lufsStandardComboBox = new QComboBox(this);
		lufsStandardComboBox->addItems(QStringList() << "ITU-R BS.1770-5" << "EBU R 128 v4.0" << "EBU Tech 3341" << "EBU Tech 3342" << "ATSC A/85" << "OP-59" << "TR-B32");
		lufsStandardComboBox->setCurrentText(tokenValue(parameters, "LUFS", tokenValue(parameters, "LUFSStandard", "ITU-R BS.1770-5")));
		QPushButton* settingsResetButton = addModuleResetButton(grid, 0, 4);
		settingsResetButton->setText(tr("Reset module"));
		grid->addWidget(panelButton, 0, 0);
		grid->addWidget(resetButton, 0, 1);
		grid->addWidget(new QLabel(tr("RMS"), this), 0, 2);
		grid->addWidget(rmsStandardComboBox, 0, 3);
		grid->addWidget(new QLabel(tr("LUFS"), this), 1, 0);
		grid->addWidget(lufsStandardComboBox, 1, 1, 1, 3);
		addChannelSelector(grid, parameters, 2, 0, 5);
		meterDialog = new QDialog(nullptr, Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
		meterDialog->setWindowTitle(tr("APO Loudness / VU Meter"));
		meterDialog->setAttribute(Qt::WA_DeleteOnClose, false);
		meterDialog->resize(980, 560);
		QVBoxLayout* panelLayout = new QVBoxLayout(meterDialog);
		QHBoxLayout* meterBodyLayout = new QHBoxLayout();
		QScrollArea* scroll = new QScrollArea(meterDialog);
		scroll->setWidgetResizable(true);
		meterPanel = new VUMeterPanel(scroll);
		scroll->setWidget(meterPanel);
		QScrollArea* statsScroll = new QScrollArea(meterDialog);
		statsScroll->setWidgetResizable(false);
		statsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		statsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		statsScroll->setMinimumWidth(440);
		meterStatsPanel = new VUMeterStatsPanel(statsScroll);
		statsScroll->setWidget(meterStatsPanel);
		meterBodyLayout->addWidget(scroll, 1);
		meterBodyLayout->addWidget(statsScroll, 0);
		panelLayout->addLayout(meterBodyLayout);
		updateMeterPanel();
		connect(panelButton, &QPushButton::toggled, this, [this](bool checked) {
			panelButton->setText(checked ? tr("Hide panel") : tr("Open panel"));
			if (checked)
			{
				meterDialog->setWindowState(meterDialog->windowState() & ~Qt::WindowMinimized);
				meterDialog->show();
				meterDialog->raise();
				meterDialog->activateWindow();
			}
			else
				meterDialog->hide();
		});
		connect(meterDialog, &QDialog::finished, this, [this](int) {
			if (panelButton != nullptr)
			{
				QSignalBlocker blocker(panelButton);
				panelButton->setChecked(false);
				panelButton->setText(tr("Open panel"));
			}
		});
		connect(resetButton, &QPushButton::clicked, this, [this]() { if (meterPanel) meterPanel->reset(); });
	}
	for (QComboBox* combo : findChildren<QComboBox*>())
		connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int) { emit updateModel(); });
	for (QCheckBox* check : findChildren<QCheckBox*>())
		connect(check, &QCheckBox::toggled, this, [this](bool) { emit updateModel(); });
}

AudioToolFilterGUI::~AudioToolFilterGUI()
{
	destroyMeterDialog();
}

void AudioToolFilterGUI::prepareDelete()
{
	destroyMeterDialog();
}

void AudioToolFilterGUI::destroyMeterDialog()
{
	if (meterDialog == nullptr)
		return;

	meterDialog->hide();
	delete meterDialog;
	meterDialog = nullptr;
	meterPanel = nullptr;
	meterStatsPanel = nullptr;
}

void AudioToolFilterGUI::updateMeterPanel()
{
	if (meterPanel != nullptr)
		meterPanel->setMeterId("default");
	if (meterStatsPanel != nullptr)
		meterStatsPanel->setMeterId("default");
}

void AudioToolFilterGUI::store(QString& command, QString& parameters)
{
	command = commandName;
	if (commandName == "ToneGenerator")
	{
		parameters = QString("State %1 Type %2 Frequency %3 Hz Level %4 dB Channels %5 Mode %6")
			.arg(stateButton->isChecked() ? 1 : 0)
			.arg(typeComboBox->currentText())
			.arg(frequencySpinBox->value())
			.arg(levelSpinBox->value())
			.arg(selectedChannels())
			.arg(modeComboBox->currentText());
		if (typeComboBox->currentText() == "Sweep")
			parameters += QString(" Start %1 Hz End %2 Hz Duration %3 s").arg(startSpinBox->value()).arg(endSpinBox->value()).arg(durationSpinBox->value());
	}
	else if (commandName == "Pan")
		parameters = QString("Position %1 Width %2").arg(positionSpinBox->value()).arg(widthSpinBox->value());
	else if (commandName == "Crossfeed")
		parameters = QString("Algorithm %1 Preset \"%2\" Amount %3 % Circumference %4 cm HeadWidth %5 cm HeadLength %6 cm Angle %7 deg Cutoff %8 Hz Direct %9 %")
			.arg(crossfeedAlgorithmComboBox != nullptr ? crossfeedAlgorithmComboBox->currentText() : "Natural")
			.arg(crossfeedPresetComboBox != nullptr ? crossfeedPresetComboBox->currentText() : "Average Male")
			.arg(amountSpinBox->value())
			.arg(headCircumferenceSpinBox->value())
			.arg(headWidthSpinBox->value())
			.arg(headLengthSpinBox->value())
			.arg(angleSpinBox->value())
			.arg(cutoffSpinBox->value())
			.arg(directSpinBox->value());
	else if (commandName == "Chorus")
		parameters = QString("Rate %1 Hz Depth %2 ms Mix %3 % Feedback %4 %").arg(rateSpinBox->value()).arg(depthSpinBox->value()).arg(mixSpinBox->value()).arg(feedbackSpinBox->value());
	else if (commandName == "Reverb")
		parameters = QString("RoomSize %1 % Damping %2 % Wet %3 % Dry %4 % Width %5 %").arg(roomSpinBox->value()).arg(dampingSpinBox->value()).arg(wetSpinBox->value()).arg(drySpinBox->value()).arg(widthSpinBox->value());
	else if (commandName == "VUMeter")
		parameters = QString("MeterId default Channels %1 RMS \"%2\" LUFS \"%3\"")
			.arg(selectedChannels())
			.arg(rmsStandardComboBox != nullptr ? rmsStandardComboBox->currentText() : "AES17")
			.arg(lufsStandardComboBox != nullptr ? lufsStandardComboBox->currentText() : "ITU-R BS.1770-5");
}

QList<FilterTemplate> ToneGeneratorFilterGUIFactory::createFilterTemplates()
{
	return QList<FilterTemplate>() << FilterTemplate(tr("Tone generator"), "ToneGenerator: State 0 Type Sine Frequency 1000 Hz Level -20 dB Channels all Mode Replace", QStringList(tr("Basic filters")));
}

IFilterGUI* ToneGeneratorFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	return command == "ToneGenerator" ? new AudioToolFilterGUI(command, parameters) : nullptr;
}

QList<FilterTemplate> PanFilterGUIFactory::createFilterTemplates()
{
	return QList<FilterTemplate>() << FilterTemplate(tr("Pan"), "Pan: Position 0 Width 100", QStringList(tr("Basic filters")));
}

IFilterGUI* PanFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	return command == "Pan" ? new AudioToolFilterGUI(command, parameters) : nullptr;
}

QList<FilterTemplate> CrossfeedFilterGUIFactory::createFilterTemplates()
{
	return QList<FilterTemplate>() << FilterTemplate(tr("Crossfeed"), "Crossfeed: Algorithm Natural Preset \"Average Male\" Amount 35 % Circumference 57 cm HeadWidth 15 cm HeadLength 19 cm Angle 60 deg Cutoff 900 Hz Direct 100 %", QStringList(tr("Effects")));
}

IFilterGUI* CrossfeedFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	return command == "Crossfeed" ? new AudioToolFilterGUI(command, parameters) : nullptr;
}

QList<FilterTemplate> ChorusFilterGUIFactory::createFilterTemplates()
{
	return QList<FilterTemplate>() << FilterTemplate(tr("Chorus"), "Chorus: Rate 0.4 Hz Depth 8 ms Mix 25 % Feedback 0 %", QStringList(tr("Effects")));
}

IFilterGUI* ChorusFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	return command == "Chorus" ? new AudioToolFilterGUI(command, parameters) : nullptr;
}

QList<FilterTemplate> ReverbFilterGUIFactory::createFilterTemplates()
{
	return QList<FilterTemplate>() << FilterTemplate(tr("Reverb"), "Reverb: RoomSize 50 % Damping 50 % Wet 20 % Dry 100 % Width 100 %", QStringList(tr("Effects")));
}

IFilterGUI* ReverbFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	return command == "Reverb" ? new AudioToolFilterGUI(command, parameters) : nullptr;
}

QList<FilterTemplate> VUMeterFilterGUIFactory::createFilterTemplates()
{
	return QList<FilterTemplate>() << FilterTemplate(tr("VU meter"), "VUMeter: MeterId default Channels all RMS \"AES17\" LUFS \"ITU-R BS.1770-5\"", QStringList(tr("Analysis")));
}

IFilterGUI* VUMeterFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	return command == "VUMeter" ? new AudioToolFilterGUI(command, parameters) : nullptr;
}
