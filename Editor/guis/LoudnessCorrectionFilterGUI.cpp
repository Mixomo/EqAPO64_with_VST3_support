/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

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
#include "LoudnessCorrectionFilterGUIDialog.h"
#include "LoudnessCorrectionFilterGUI.h"
#include "ui_LoudnessCorrectionFilterGUI.h"
#include <QLayoutItem>
#include <QPushButton>
#include <QVariant>

LoudnessCorrectionFilterGUI::LoudnessCorrectionFilterGUI(double refLevel, double refOffset, double att)
	: IFilterGUI(),
	ui(new Ui::LoudnessCorrectionFilterGUI)
{
	ui->setupUi(this);

	ui->refLevelDial->setFixedSize(GUIHelper::scale(QSize(100, 66)));
	ui->refOffsetDial->setFixedSize(GUIHelper::scale(QSize(100, 66)));
	ui->attDial->setFixedSize(GUIHelper::scale(QSize(100, 66)));

	ui->refLevelSpinBox->setValue((int)refLevel);
	ui->refOffsetSpinBox->setValue((int)refOffset);
	ui->attSpinBox->setValue(att);
	ui->refLevelSpinBox->setProperty("defaultValue", 0);
	ui->refOffsetSpinBox->setProperty("defaultValue", 0);
	ui->attSpinBox->setProperty("defaultValue", 1.0);
	ui->refLevelDial->setProperty("resetTarget", QVariant::fromValue(static_cast<QObject*>(ui->refLevelSpinBox)));
	ui->refLevelDial->setProperty("defaultTargetValue", 0);
	ui->refOffsetDial->setProperty("resetTarget", QVariant::fromValue(static_cast<QObject*>(ui->refOffsetSpinBox)));
	ui->refOffsetDial->setProperty("defaultTargetValue", 0);
	ui->attDial->setProperty("resetTarget", QVariant::fromValue(static_cast<QObject*>(ui->attSpinBox)));
	ui->attDial->setProperty("defaultTargetValue", 1.0);

	QPushButton* resetButton = new QPushButton(tr("Reset"), this);
	if (QLayoutItem* spacer = ui->gridLayout->itemAtPosition(0, 12))
	{
		ui->gridLayout->removeItem(spacer);
		delete spacer;
	}
	ui->gridLayout->addWidget(resetButton, 0, 12, 2, 1);
	connect(resetButton, &QPushButton::clicked, this, [this]() {
		state = true;
		ui->refLevelSpinBox->setValue(0);
		ui->refOffsetSpinBox->setValue(0);
		ui->attSpinBox->setValue(1.0);
	});

	connect(&timer, SIGNAL(timeout()), this, SLOT(updateVolume()));
	timer.start(10);
}

LoudnessCorrectionFilterGUI::~LoudnessCorrectionFilterGUI()
{
	delete ui;
}

void LoudnessCorrectionFilterGUI::store(QString& command, QString& parameters)
{
	command = "LoudnessCorrection";
	parameters = QString("State %0 ReferenceLevel %1 ReferenceOffset %2 Attenuation ").arg(state ? 1 : 0).arg(ui->refLevelSpinBox->value()).arg(ui->refOffsetSpinBox->value());
	double att = ui->attSpinBox->value();
	if (att == 0.0 || att == 1.0)
		parameters += QString("%0").arg(att, 0, 'f', 1);
	else
		parameters += QString("%0").arg(att);
}

void LoudnessCorrectionFilterGUI::on_refLevelSpinBox_valueChanged(int value)
{
	emit updateModel();
}

void LoudnessCorrectionFilterGUI::on_refOffsetSpinBox_valueChanged(int value)
{
	emit updateModel();
}

void LoudnessCorrectionFilterGUI::on_attDial_valueChanged(int value)
{
	ui->attSpinBox->setValue(value / 100.0);
}

void LoudnessCorrectionFilterGUI::on_attSpinBox_valueChanged(double value)
{
	bool previousValue = ui->attDial->blockSignals(true);
	ui->attDial->setValue(round(value * 100.0));
	ui->attDial->blockSignals(previousValue);

	emit updateModel();
}

void LoudnessCorrectionFilterGUI::on_calibrateButton_clicked()
{
	state = false;
	emit updateModel();

	LoudnessCorrectionFilterGUIDialog dialog;
	if (dialog.exec() == QDialog::Accepted)
	{
		updateVolume();
		double refLevel = 75.0 - dialog.getMeasuredLevel() + lastVolume;
		ui->refLevelSpinBox->setValue(refLevel);
	}

	state = true;
	emit updateModel();
}

void LoudnessCorrectionFilterGUI::updateVolume()
{
	double volume;
	HRESULT res = volumeController.getVolume(volume);

	if (SUCCEEDED(res) && volume != lastVolume)
	{
		ui->volumeSpinBox->setValue(volume);
		lastVolume = volume;
	}
}
