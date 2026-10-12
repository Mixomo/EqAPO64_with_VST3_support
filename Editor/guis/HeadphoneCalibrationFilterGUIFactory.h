#pragma once

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QVector>

#include "Editor/IFilterGUIFactory.h"

class FilterTable;

class HeadphoneCalibrationFilterGUI : public IFilterGUI
{
	Q_OBJECT

public:
	explicit HeadphoneCalibrationFilterGUI(const QString& parameters, FilterTable* filterTable);
	void store(QString& command, QString& parameters) override;

	struct Band
	{
		double frequency = 0.0;
		double gain = 0.0;
	};

private:
	struct AshFilter
	{
		QString source;
		QString brand;
		QString model;
		QString sample;
		QString type;
		QVector<Band> correction;
	};

	void loadAshCatalog();
	void refreshSources();
	void refreshBrands();
	void refreshModels();
	void loadSelectedFilter();
	void addGraphicEQModule();
	void addConvolutionModule();
	void exportGraphicEQ();
	void exportFIR();
	void addParametricEQModule();
	void exportParametricEQ();
	bool exportFIRToPath(const QVector<Band>& sourceBands, const QString& path);
	QString graphicEQLine(const QVector<Band>& sourceBands) const;
	QString parametricEQLine(const QVector<Band>& sourceBands) const;
	QVector<Band> normalizedCorrection(QVector<Band> result) const;
	unsigned currentDeviceSampleRate() const;
	void setStatus(const QString& text);

	FilterTable* filterTable = nullptr;
	QComboBox* sourceComboBox = nullptr;
	QComboBox* brandComboBox = nullptr;
	QComboBox* modelComboBox = nullptr;
	QPushButton* addGraphicButton = nullptr;
	QPushButton* addConvolutionButton = nullptr;
	QPushButton* addParametricButton = nullptr;
	QPushButton* exportGraphicButton = nullptr;
	QPushButton* exportFirButton = nullptr;
	QPushButton* exportParametricButton = nullptr;
	QLabel* statusLabel = nullptr;
	QString catalogDir;
	QVector<AshFilter> ashFilters;
	QVector<int> visibleModels;
	QVector<Band> ashCorrectionBands;
	QString selectedSource;
	QString selectedBrand;
	QString selectedModel;
};

class HeadphoneCalibrationFilterGUIFactory : public IFilterGUIFactory
{
	Q_OBJECT

public:
	void initialize(FilterTable* filterTable) override;
	QList<FilterTemplate> createFilterTemplates() override;
	IFilterGUI* createFilterGUI(QString& command, QString& parameters) override;

private:
	FilterTable* filterTable = nullptr;
};
