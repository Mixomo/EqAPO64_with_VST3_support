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
#include <QProcess>
#include "Editor/IFilterGUI.h"
#include "helpers/VSTPluginLibrary.h"
#include "Editor/helpers/ManagedGuiProcess.h"

namespace Ui {
class VSTPluginFilterGUI;
}
namespace EqApoControl {struct NativeViewSnapshot;}

class VSTPluginFilterGUI : public IFilterGUI
{
	Q_OBJECT

public:
    // Bypass affects audio, not the ability to hide/show/close the retained panel.
    void setModuleBypassed(bool) override {setEnabled(true);}
	explicit VSTPluginFilterGUI(std::shared_ptr<VSTPluginLibrary> library, const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap, const QString& hostId = QString(), int vst3ClassIndex = 0, const std::wstring& classUID = L"");
	~VSTPluginFilterGUI();

	void store(QString& command, QString& parameters) override;
	void loadPreferences(const QVariantMap& prefs) override;
	void storePreferences(QVariantMap& prefs) override;
	void prepareDelete() override;
    bool flushPendingEdits() override;
    ManagedGuiProcess::Shutdown getGuiShutdownDiagnostics() const noexcept {return lastGuiProcess?lastGuiProcess->shutdown():ManagedGuiProcess::Shutdown{};}
    DWORD getOwnedGuiProcessId() const noexcept {return guiProcess?guiProcess->processId():0;}

private slots:
	void on_openPanelButton_clicked();
	void on_reloadButton_clicked();
	void on_vst3ClassComboBox_currentIndexChanged(int index);
	void on_pathLineEdit_editingFinished();
	void on_selectButton_clicked();
	void on_idle();

private:
    EqApoControl::NativeViewSnapshot nativePanelControl(unsigned command=0,bool consume=false);
    void scanProtectedPlugin();
	void openOutProcPanel();
	bool signalOutProcPanel(const wchar_t* suffix);
	bool consumeOutProcPanelSignal(const wchar_t* suffix);
	void closeOutProcPanel();
	void terminateOutProcPanel();
	void refreshVST3ClassComboBox();

	Ui::VSTPluginFilterGUI* ui;
	std::shared_ptr<VSTPluginLibrary> library;
	QTimer idleTimer;
	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	QString hostId;
	bool outProcGuiRunning = false;
    bool nativeDspPanel=false,nativeViewOpening=false;
    bool nativeClosePending=false;
    bool nativeStatePending=false;
    bool nativeStateSaveFailed=false;
    std::uint64_t nativePanelGeneration=0;
	qint64 outProcGuiPid = 0;
	QString outProcGuiConfigPath;
	bool outProcGuiHidden = false;
    bool needsControlIdentitySave=false;
	QElapsedTimer lastReadTimer;
	int vst3ClassIndex = 0;
    std::wstring selectedClassUID;
	QProcess* scanner=nullptr;
    std::shared_ptr<ManagedGuiProcess> guiProcess,lastGuiProcess;
    bool pendingPanelOpen=false;
    unsigned nativeEndpointRetries=0;
    std::uint64_t lastStateGeneration=~std::uint64_t(0);
    std::uint64_t nativeSnapshotGeneration=0,nativeSnapshotSequence=0;
};
