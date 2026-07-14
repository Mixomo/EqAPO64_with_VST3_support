#pragma once

#include <QList>
#include <QMouseEvent>
#include <QSlider>
#include <functional>
#include "Editor/IFilterGUIFactory.h"
#include "filters/ParametricEQFilter.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;

class ParametricEQSlider : public QSlider
{
public:
	explicit ParametricEQSlider(Qt::Orientation orientation, QWidget* parent = nullptr);
	std::function<void()> resetHandler;

protected:
	void mouseDoubleClickEvent(QMouseEvent* event) override;
};

class ParametricEQFilterGUI : public IFilterGUI
{
	Q_OBJECT

public:
	explicit ParametricEQFilterGUI(const std::vector<ParametricEQFilter::Band>& bands);
	~ParametricEQFilterGUI();
	void store(QString& command, QString& parameters) override;

private:
	struct Row
	{
		QWidget* widget = nullptr;
		QLabel* index = nullptr;
		QCheckBox* enabled = nullptr;
		QComboBox* type = nullptr;
		QSlider* freqSlider = nullptr;
		QDoubleSpinBox* freq = nullptr;
		QSlider* gainSlider = nullptr;
		QDoubleSpinBox* gain = nullptr;
		QSlider* qSlider = nullptr;
		QDoubleSpinBox* q = nullptr;
		QPushButton* remove = nullptr;
		ParametricEQFilter::Band defaultBand;
	};

	void addBand(const ParametricEQFilter::Band& band);
	void rebuildRows();
	void scrollToRow(Row* row);
	void removeRow(Row* row);
	void sortRows();
	void resetToDefaults();
	void importText();
	void exportText();
	QString bandText(const Row* row) const;
	void connectSliderPair(QSlider* slider, QDoubleSpinBox* spin, double min, double max, bool logarithmic);
	void setRowValues(Row* row, const ParametricEQFilter::Band& band);
	static int sliderFromValue(double value, double min, double max, bool logarithmic);
	static double valueFromSlider(int value, double min, double max, bool logarithmic);

	QVBoxLayout* rowsLayout = nullptr;
	QScrollArea* scrollArea = nullptr;
	QList<Row*> rows;
	std::vector<ParametricEQFilter::Band> defaultBands;
};

class ParametricEQFilterGUIFactory : public IFilterGUIFactory
{
	Q_OBJECT

public:
	QList<FilterTemplate> createFilterTemplates() override;
	IFilterGUI* createFilterGUI(QString& command, QString& parameters) override;
};
