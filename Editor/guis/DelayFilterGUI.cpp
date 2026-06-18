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

#include "Editor/helpers/GUIHelper.h"
#include "DelayFilterGUI.h"
#include "ui_DelayFilterGUI.h"
#include <QLayoutItem>
#include <QPushButton>
#include <QVariant>

static const double dialSteps = 1000;
static const double dialMin = 1.0;
static const double dialMax = 10000.0;

DelayFilterGUI::DelayFilterGUI(double delay, bool isMs)
	: ui(new Ui::DelayFilterGUI)
{
	ui->setupUi(this);

	ui->delayDial->setFixedSize(GUIHelper::scale(QSize(100, 66)));
	ui->unitComboBox->setCurrentIndex(isMs ? 0 : 1);
	ui->delaySpinBox->setValue(delay);
	ui->delaySpinBox->setProperty("defaultValue", 0.0);
	ui->delayDial->setProperty("resetTarget", QVariant::fromValue(static_cast<QObject*>(ui->delaySpinBox)));
	ui->delayDial->setProperty("defaultTargetValue", 0.0);

	QPushButton* resetButton = new QPushButton(tr("Reset"), this);
	if (QLayoutItem* spacer = ui->gridLayout->itemAtPosition(0, 3))
	{
		ui->gridLayout->removeItem(spacer);
		delete spacer;
	}
	ui->gridLayout->addWidget(resetButton, 0, 3, 2, 1);
	connect(resetButton, &QPushButton::clicked, this, [this]() {
		ui->unitComboBox->setCurrentIndex(0);
		ui->delaySpinBox->setValue(0.0);
	});
}

DelayFilterGUI::~DelayFilterGUI()
{
	delete ui;
}

void DelayFilterGUI::store(QString& command, QString& parameters)
{
	command = "Delay";
	parameters = QString("%0").arg(ui->delaySpinBox->value());
	if (ui->unitComboBox->currentIndex() == 0)
		parameters += " ms";
	else
		parameters += " samples";
}

void DelayFilterGUI::on_unitComboBox_currentIndexChanged(int index)
{
	ui->delaySpinBox->setSuffix(index == 0 ? tr(" ms") : tr(" samples"));
	ui->delaySpinBox->setDecimals(index == 0 ? 2 : 0);

	emit updateModel();
}

void DelayFilterGUI::on_delayDial_valueChanged(int value)
{
	ui->delaySpinBox->setValue(pow(dialMax / dialMin, value / dialSteps) * dialMin);
}

void DelayFilterGUI::on_delaySpinBox_valueChanged(double value)
{
	bool previousValue = ui->delayDial->blockSignals(true);
	ui->delayDial->setValue(value <= 0.0 ? 0 : round(dialSteps * log(value / dialMin) / log(dialMax / dialMin)));
	ui->delayDial->blockSignals(previousValue);

	emit updateModel();
}
