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

#pragma once

#include <memory>
#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include "Editor/IFilterGUI.h"
#include "helpers/VSTPluginLibrary.h"

namespace Ui {
class VSTPluginFilterGUI;
}

class VSTPluginFilterGUI : public IFilterGUI
{
	Q_OBJECT

public:
	explicit VSTPluginFilterGUI(std::shared_ptr<VSTPluginLibrary> library, const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap, bool outProcMode = false, const QString& hostId = QString(), int vst3ClassIndex = 0);
	~VSTPluginFilterGUI();

	void store(QString& command, QString& parameters) override;
	void loadPreferences(const QVariantMap& prefs) override;
	void storePreferences(QVariantMap& prefs) override;
	void prepareDelete() override;
	void onAutomate();
	void onSizeWindow(int w, int h);

private slots:
	void on_openPanelButton_clicked();
	void on_reloadButton_clicked();
	void on_vst3ClassComboBox_currentIndexChanged(int index);
	void applyDialog();
	void autoApplyToggled(bool checked);
	void on_pathLineEdit_editingFinished();
	void on_selectButton_clicked();
	void on_idle();

private:
	void initPlugin();
	void openOutProcPanel();
	bool signalOutProcPanel(const wchar_t* suffix);
	bool consumeOutProcPanelSignal(const wchar_t* suffix);
	void closeOutProcPanel();
	void terminateOutProcPanel();
	void releasePluginInstance();
	void refreshVST3ClassComboBox();
	void updatePermissionWarning();

	Ui::VSTPluginFilterGUI* ui;
	std::shared_ptr<VSTPluginLibrary> library;
	VSTPluginInstance* effect = NULL;
	QTimer idleTimer;
	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	bool outProcMode = false;
	QString hostId;
	bool outProcGuiRunning = false;
	qint64 outProcGuiPid = 0;
	QString outProcGuiConfigPath;
	bool outProcGuiHidden = false;
	bool autoApplyDialog = false;
	QElapsedTimer lastReadTimer;
	int vst3ClassIndex = 0;
};
