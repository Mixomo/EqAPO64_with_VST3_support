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

#include <QToolButton>
#include <QVector>
#include "Editor/IFilterGUI.h"

namespace Ui {
class ConvolutionFilterGUI;
}

class ConvolutionFilterGUI : public IFilterGUI
{
	Q_OBJECT

public:
	explicit ConvolutionFilterGUI(const QString& configPath, unsigned deviceSampleRate, const QString& deviceGuid, const QString& path);
	~ConvolutionFilterGUI();

	void store(QString& command, QString& parameters) override;

private slots:
	void on_selectFileToolButton_clicked();

	void on_pathLineEdit_editingFinished();

private:
	QString absoluteImpulsePath() const;
	unsigned refreshDeviceSampleRate() const;
	unsigned liveDeviceSampleRate() const;
	bool matchDeviceSampleRate(bool interactive = true);
	void populateBundledImpulseResponses();
	void selectBundledImpulseResponse(const QString& absolutePath);
	void selectBundledImpulseAt(int index);
	int currentBundledImpulseIndex();
	void updateFileInfo();

	Ui::ConvolutionFilterGUI* ui;
	QString configPath;
	QString deviceGuid;
	unsigned deviceSampleRate;
	QToolButton* bundledIrButton = nullptr;
	QVector<QString> bundledImpulsePaths;
	int currentBundledImpulseListIndex = -1;
	bool autoMatchingSampleRate = false;
};
