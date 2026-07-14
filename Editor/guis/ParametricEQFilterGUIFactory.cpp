#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextStream>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "ParametricEQFilterGUIFactory.h"
#include "filters/ParametricEQFilterFactory.h"

static QString typeName(BiQuad::Type type)
{
	if (type == BiQuad::LOW_SHELF)
		return "LSQ";
	if (type == BiQuad::HIGH_SHELF)
		return "HSQ";
	return "PK";
}

static void styleSlider(QSlider* slider)
{
	slider->setStyleSheet(
		"QSlider::groove:horizontal{height:6px;background:#26323a;border-radius:3px;}"
		"QSlider::sub-page:horizontal{background:#36d6a3;border-radius:3px;}"
		"QSlider::handle:horizontal{width:14px;margin:-5px 0;background:#d9e2e8;border:1px solid #5b6870;border-radius:7px;}");
}

ParametricEQSlider::ParametricEQSlider(Qt::Orientation orientation, QWidget* parent)
	: QSlider(orientation, parent)
{
}

void ParametricEQSlider::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (resetHandler)
	{
		resetHandler();
		event->accept();
		return;
	}
	QSlider::mouseDoubleClickEvent(event);
}

ParametricEQFilterGUI::ParametricEQFilterGUI(const std::vector<ParametricEQFilter::Band>& bands)
{
	defaultBands = bands;
	if (defaultBands.empty())
	{
		ParametricEQFilter::Band band;
		defaultBands.push_back(band);
	}

	QGridLayout* root = new QGridLayout(this);
	root->setContentsMargins(2, 2, 2, 2);
	root->setHorizontalSpacing(10);
	root->setVerticalSpacing(8);
	setMinimumWidth(1480);

	QWidget* header = new QWidget(this);
	QGridLayout* headerLayout = new QGridLayout(header);
	headerLayout->setContentsMargins(0, 0, 0, 0);
	headerLayout->setHorizontalSpacing(10);
	headerLayout->addWidget(new QLabel(tr("On"), header), 0, 0);
	headerLayout->addWidget(new QLabel(tr("Type"), header), 0, 1);
	headerLayout->addWidget(new QLabel(tr("Frequency"), header), 0, 2);
	headerLayout->addWidget(new QLabel(tr("Gain"), header), 0, 3);
	headerLayout->addWidget(new QLabel(tr("Q"), header), 0, 4);
	root->addWidget(header, 0, 0, 1, 5);

	QWidget* rowsWidget = new QWidget(this);
	rowsLayout = new QVBoxLayout(rowsWidget);
	rowsLayout->setContentsMargins(0, 0, 14, 0);
	rowsLayout->setSpacing(4);
	rowsLayout->addStretch(1);

	scrollArea = new QScrollArea(this);
	scrollArea->setWidget(rowsWidget);
	scrollArea->setWidgetResizable(true);
	scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	scrollArea->setMinimumHeight(210);
	scrollArea->setMaximumHeight(430);
	scrollArea->setMinimumWidth(1180);
	scrollArea->setFrameShape(QFrame::StyledPanel);
	scrollArea->setLineWidth(2);
	root->addWidget(scrollArea, 1, 0, 1, 5);

	QPushButton* addButton = new QPushButton(tr("Add filter"), this);
	QPushButton* sortButton = new QPushButton(tr("Sort"), this);
	QPushButton* resetButton = new QPushButton(tr("Reset filters"), this);
	QPushButton* importButton = new QPushButton(tr("Import ParametricEQ"), this);
	QPushButton* exportButton = new QPushButton(tr("Export ParametricEQ"), this);
	addButton->setMinimumWidth(140);
	sortButton->setMinimumWidth(140);
	resetButton->setMinimumWidth(140);
	importButton->setMinimumWidth(170);
	exportButton->setMinimumWidth(170);
	root->addWidget(addButton, 2, 0);
	root->addWidget(sortButton, 2, 1);
	root->addWidget(resetButton, 2, 2);
	root->addWidget(importButton, 2, 3);
	root->addWidget(exportButton, 2, 4);

	connect(addButton, &QPushButton::clicked, this, [this]() {
		ParametricEQFilter::Band band;
		addBand(band);
		scrollToRow(rows.last());
		emit updateModel();
	});
	connect(sortButton, &QPushButton::clicked, this, &ParametricEQFilterGUI::sortRows);
	connect(resetButton, &QPushButton::clicked, this, &ParametricEQFilterGUI::resetToDefaults);
	connect(importButton, &QPushButton::clicked, this, &ParametricEQFilterGUI::importText);
	connect(exportButton, &QPushButton::clicked, this, &ParametricEQFilterGUI::exportText);

	if (bands.empty())
	{
		ParametricEQFilter::Band band;
		addBand(band);
	}
	else
	{
		for (const ParametricEQFilter::Band& band : bands)
			addBand(band);
	}
}

ParametricEQFilterGUI::~ParametricEQFilterGUI()
{
	qDeleteAll(rows);
	rows.clear();
}

int ParametricEQFilterGUI::sliderFromValue(double value, double min, double max, bool logarithmic)
{
	value = std::max(min, std::min(max, value));
	if (logarithmic)
		return static_cast<int>(std::round(std::log(value / min) / std::log(max / min) * 1000.0));
	return static_cast<int>(std::round((value - min) / (max - min) * 1000.0));
}

double ParametricEQFilterGUI::valueFromSlider(int value, double min, double max, bool logarithmic)
{
	const double t = value / 1000.0;
	if (logarithmic)
		return min * std::pow(max / min, t);
	return min + (max - min) * t;
}

void ParametricEQFilterGUI::connectSliderPair(QSlider* slider, QDoubleSpinBox* spin, double min, double max, bool logarithmic)
{
	slider->setRange(0, 1000);
	slider->setValue(sliderFromValue(spin->value(), min, max, logarithmic));
	connect(slider, &QSlider::valueChanged, this, [this, spin, min, max, logarithmic](int value) {
		QSignalBlocker blocker(spin);
		spin->setValue(valueFromSlider(value, min, max, logarithmic));
		emit updateModel();
	});
	connect(spin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [this, slider, min, max, logarithmic](double value) {
		QSignalBlocker blocker(slider);
		slider->setValue(sliderFromValue(value, min, max, logarithmic));
		emit updateModel();
	});
}

void ParametricEQFilterGUI::setRowValues(Row* row, const ParametricEQFilter::Band& band)
{
	QSignalBlocker enabledBlocker(row->enabled);
	QSignalBlocker typeBlocker(row->type);
	QSignalBlocker freqBlocker(row->freq);
	QSignalBlocker gainBlocker(row->gain);
	QSignalBlocker qBlocker(row->q);
	row->enabled->setChecked(band.enabled);
	row->type->setCurrentText(typeName(band.type));
	row->freq->setValue(std::max(20.0, std::min(20000.0, band.freq)));
	row->gain->setValue(std::max(-24.0, std::min(24.0, band.gain)));
	row->q->setValue(std::max(0.10, std::min(18.0, band.q)));
	row->freqSlider->setValue(sliderFromValue(row->freq->value(), 20.0, 20000.0, true));
	row->gainSlider->setValue(sliderFromValue(row->gain->value(), -24.0, 24.0, false));
	row->qSlider->setValue(sliderFromValue(row->q->value(), 0.10, 18.0, true));
	emit updateModel();
}

void ParametricEQFilterGUI::addBand(const ParametricEQFilter::Band& band)
{
	Row* row = new Row;
	row->defaultBand = band;
	row->widget = new QWidget(this);
	QGridLayout* grid = new QGridLayout(row->widget);
	grid->setContentsMargins(0, 2, 0, 2);
	grid->setHorizontalSpacing(10);

	row->index = new QLabel(row->widget);
	row->index->setMinimumWidth(34);
	row->index->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	row->enabled = new QCheckBox(row->widget);
	row->enabled->setChecked(band.enabled);
	row->type = new QComboBox(row->widget);
	row->type->addItems(QStringList() << "PK" << "LSQ" << "HSQ");
	row->type->setCurrentText(typeName(band.type));

	row->freqSlider = new ParametricEQSlider(Qt::Horizontal, row->widget);
	row->freq = new QDoubleSpinBox(row->widget);
	row->freq->setRange(20.0, 20000.0);
	row->freq->setDecimals(1);
	row->freq->setSuffix(" Hz");
	row->freq->setValue(std::max(20.0, std::min(20000.0, band.freq)));
	row->freq->setProperty("defaultValue", 1000.0);

	row->gainSlider = new ParametricEQSlider(Qt::Horizontal, row->widget);
	row->gain = new QDoubleSpinBox(row->widget);
	row->gain->setRange(-24.0, 24.0);
	row->gain->setDecimals(2);
	row->gain->setSuffix(" dB");
	row->gain->setValue(std::max(-24.0, std::min(24.0, band.gain)));
	row->gain->setProperty("defaultValue", 0.0);

	row->qSlider = new ParametricEQSlider(Qt::Horizontal, row->widget);
	row->q = new QDoubleSpinBox(row->widget);
	row->q->setRange(0.10, 18.0);
	row->q->setDecimals(2);
	row->q->setSuffix(" Q");
	row->q->setValue(std::max(0.10, std::min(18.0, band.q)));
	row->q->setProperty("defaultValue", 1.0);

	row->remove = new QPushButton(tr("-"), row->widget);
	row->remove->setFixedWidth(32);
	row->type->setMinimumWidth(72);
	row->freqSlider->setMinimumWidth(190);
	row->gainSlider->setMinimumWidth(190);
	row->qSlider->setMinimumWidth(190);
	styleSlider(row->freqSlider);
	styleSlider(row->gainSlider);
	styleSlider(row->qSlider);

	grid->addWidget(row->index, 0, 0);
	grid->addWidget(row->enabled, 0, 1);
	grid->addWidget(row->type, 0, 2);
	grid->addWidget(row->freqSlider, 0, 3);
	grid->addWidget(row->freq, 0, 4);
	grid->addWidget(row->gainSlider, 0, 5);
	grid->addWidget(row->gain, 0, 6);
	grid->addWidget(row->qSlider, 0, 7);
	grid->addWidget(row->q, 0, 8);
	grid->addWidget(row->remove, 0, 9);
	grid->setColumnStretch(3, 1);
	grid->setColumnStretch(5, 1);
	grid->setColumnStretch(7, 1);

	connectSliderPair(row->freqSlider, row->freq, 20.0, 20000.0, true);
	connectSliderPair(row->gainSlider, row->gain, -24.0, 24.0, false);
	connectSliderPair(row->qSlider, row->q, 0.10, 18.0, true);
	row->freqSlider->setProperty("resetTarget", QVariant::fromValue(static_cast<QObject*>(row->freq)));
	row->freqSlider->setProperty("defaultTargetValue", 1000.0);
	row->gainSlider->setProperty("resetTarget", QVariant::fromValue(static_cast<QObject*>(row->gain)));
	row->gainSlider->setProperty("defaultTargetValue", 0.0);
	row->qSlider->setProperty("resetTarget", QVariant::fromValue(static_cast<QObject*>(row->q)));
	row->qSlider->setProperty("defaultTargetValue", 1.0);
	static_cast<ParametricEQSlider*>(row->freqSlider)->resetHandler = [this, row]() { row->freq->setValue(1000.0); emit updateModel(); };
	static_cast<ParametricEQSlider*>(row->gainSlider)->resetHandler = [this, row]() { row->gain->setValue(0.0); emit updateModel(); };
	static_cast<ParametricEQSlider*>(row->qSlider)->resetHandler = [this, row]() { row->q->setValue(1.0); emit updateModel(); };
	connect(row->enabled, &QCheckBox::toggled, this, [this](bool) { emit updateModel(); });
	connect(row->type, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int) { emit updateModel(); });
	connect(row->remove, &QPushButton::clicked, this, [this, row]() { removeRow(row); });

	rows.append(row);
	rebuildRows();
}

void ParametricEQFilterGUI::rebuildRows()
{
	if (rowsLayout == nullptr)
		return;
	while (rowsLayout->count() > 1)
	{
		QLayoutItem* item = rowsLayout->takeAt(0);
		if (item != nullptr)
			delete item;
	}
	for (Row* row : rows)
	{
		row->index->setText(QString("%1.").arg(rows.indexOf(row) + 1));
		rowsLayout->insertWidget(rowsLayout->count() - 1, row->widget);
	}
}

void ParametricEQFilterGUI::scrollToRow(Row* row)
{
	if (row == nullptr || scrollArea == nullptr || !rows.contains(row))
		return;
	QTimer::singleShot(0, this, [this, row]() {
		if (!rows.contains(row))
			return;
		scrollArea->ensureWidgetVisible(row->widget, 0, 0);
		QTimer::singleShot(0, this, [this, row]() {
			if (rows.contains(row))
				scrollArea->ensureWidgetVisible(row->widget, 0, 0);
		});
	});
}

void ParametricEQFilterGUI::removeRow(Row* row)
{
	if (rows.size() <= 1)
		return;
	const int rowIndex = rows.indexOf(row);
	rows.removeOne(row);
	row->widget->deleteLater();
	delete row;
	rebuildRows();
	if (!rows.isEmpty())
	{
		const int lastIndex = static_cast<int>(rows.size()) - 1;
		scrollToRow(rows[std::min(rowIndex, lastIndex)]);
	}
	emit updateModel();
}

void ParametricEQFilterGUI::sortRows()
{
	std::sort(rows.begin(), rows.end(), [](const Row* a, const Row* b) {
		return a->freq->value() < b->freq->value();
	});
	rebuildRows();
	emit updateModel();
}

void ParametricEQFilterGUI::resetToDefaults()
{
	while (!rows.isEmpty())
	{
		Row* row = rows.takeLast();
		row->widget->deleteLater();
		delete row;
	}
	for (const ParametricEQFilter::Band& band : defaultBands)
		addBand(band);
	rebuildRows();
	emit updateModel();
}

QString ParametricEQFilterGUI::bandText(const Row* row) const
{
	return QString("%1 %2 Fc %3 Hz Gain %4 dB Q %5")
		.arg(row->enabled->isChecked() ? "ON" : "OFF")
		.arg(row->type->currentText())
		.arg(row->freq->value(), 0, 'f', row->freq->value() < 100.0 ? 1 : 0)
		.arg(row->gain->value(), 0, 'f', 2)
		.arg(row->q->value(), 0, 'f', 3);
}

void ParametricEQFilterGUI::importText()
{
	const QString path = QFileDialog::getOpenFileName(this, tr("Import ParametricEQ"), QString(), tr("Text files (*.txt);;All files (*.*)"));
	if (path.isEmpty())
		return;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	QString text = QString::fromUtf8(file.readAll()).trimmed();
	const int colon = text.indexOf(':');
	if (colon >= 0)
		text = text.mid(colon + 1);
	const std::vector<ParametricEQFilter::Band> imported = ParametricEQFilterFactory::parseBands(text.toStdWString());
	if (imported.empty())
		return;
	while (!rows.isEmpty())
	{
		Row* row = rows.takeLast();
		row->widget->deleteLater();
		delete row;
	}
	for (const ParametricEQFilter::Band& band : imported)
		addBand(band);
	rebuildRows();
	emit updateModel();
}

void ParametricEQFilterGUI::exportText()
{
	QString command;
	QString parameters;
	store(command, parameters);
	const QString line = command + ": " + parameters;
	QApplication::clipboard()->setText(line);

	const QString path = QFileDialog::getSaveFileName(this, tr("Export ParametricEQ"), "ParametricEQ.txt", tr("Text files (*.txt);;All files (*.*)"));
	if (path.isEmpty())
		return;
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return;
	QTextStream stream(&file);
	stream << line << "\n";
	file.commit();
}

void ParametricEQFilterGUI::store(QString& command, QString& parameters)
{
	command = "ParametricEQ";
	QStringList bands;
	for (const Row* row : rows)
		bands << bandText(row);
	parameters = bands.join("; ");
}

QList<FilterTemplate> ParametricEQFilterGUIFactory::createFilterTemplates()
{
	return QList<FilterTemplate>() << FilterTemplate(tr("Parametric equalizer"), "ParametricEQ: ON PK Fc 1000 Hz Gain 0 dB Q 1", QStringList(tr("Parametric equalizer")));
}

IFilterGUI* ParametricEQFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	if (command != "ParametricEQ")
		return nullptr;
	std::vector<ParametricEQFilter::Band> bands = ParametricEQFilterFactory::parseBands(parameters.toStdWString());
	return new ParametricEQFilterGUI(bands);
}
