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

#include <QFileInfo>
#include <QFileDialog>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QUuid>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "outproc/OutProcAudioProtocol.h"
#include "outproc/OutProcVSTConfig.h"
#include "helpers/aeffectx.h"
#include "helpers/StringHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/MainWindow.h"
#include "VSTPluginFilterGUIDialog.h"
#include "VSTPluginFilterGUI.h"
#include "ui_VSTPluginFilterGUI.h"

using namespace std;
using namespace std::placeholders;

static QString makeOutProcObjectName(const QString& hostId, const wchar_t* suffix)
{
	QString safeId = hostId;
	for (QChar& ch : safeId)
	{
		const bool ok = ch.isLetterOrNumber() || ch == '-' || ch == '_';
		if (!ok)
			ch = '_';
	}
	return "Global\\EqApoOutProcVST_" + safeId + "_" + QString::fromWCharArray(suffix);
}

static void appendOutProcDebugLog(const QString& message)
{
	QFile file(QDir::temp().absoluteFilePath("EqApoOutProcHost-debug.log"));
	if (file.open(QIODevice::Append | QIODevice::Text))
	{
		QTextStream stream(&file);
		stream << "[editor pid=" << GetCurrentProcessId() << "] " << message << "\n";
	}
}

VSTPluginFilterGUI::VSTPluginFilterGUI(std::shared_ptr<VSTPluginLibrary> library, const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap, bool outProcMode, const QString& hostId, int vst3ClassIndex)
	: ui(new Ui::VSTPluginFilterGUI), library(library), chunkData(chunkData), paramMap(paramMap), outProcMode(outProcMode), hostId(hostId), vst3ClassIndex(vst3ClassIndex)
{
	ui->setupUi(this);
	if (outProcMode && this->hostId.isEmpty())
		this->hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	ui->frame->setVisible(false);
	if (QGridLayout* grid = qobject_cast<QGridLayout*>(layout()))
	{
		QLabel* note = new QLabel(tr("NOTE: The VST module is not universally compatible with all VSTs on the market.\n\n"
			"If you experience:\n\n"
			"- Audio popping\n"
			"- Editor.exe crashes and closes\n"
			"- Audio changes you've made in the VST GUI aren't applied\n"
			"- The GUI doesn't open when you click the button\n"
			"- Audio artifacts or delays when processing changes\n\n"
			"Then the VST is partially or completely incompatible with APO's architecture and the technical limitations of the Windows audio engine.\n\n"
			"Try different VST plug-ins with both the traditional loader and the out-of-process loader; some plug-ins work better in one mode than the other."), this);
		note->setWordWrap(true);
		grid->addWidget(note, 4, 0, 1, 5);
	}
	updatePermissionWarning();

	QString absolutePath = QString::fromStdWString(library->getLibPath());
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
	QString relativePath = QDir::toNativeSeparators(pluginsDir.relativeFilePath(absolutePath));
	if (relativePath.startsWith(QDir::toNativeSeparators("../../")))
		relativePath = absolutePath;
	ui->pathLineEdit->setText(relativePath);
	refreshVST3ClassComboBox();

	connect(&idleTimer, &QTimer::timeout, this, &VSTPluginFilterGUI::on_idle);
	idleTimer.setTimerType(Qt::PreciseTimer);
	idleTimer.setInterval(16);
}

VSTPluginFilterGUI::~VSTPluginFilterGUI()
{
	closeOutProcPanel();
	releasePluginInstance();

	delete ui;
}

void VSTPluginFilterGUI::store(QString& command, QString& parameters)
{
	command = outProcMode ? "OutProcVSTPlugin" : "VSTPlugin";

	QString absolutePath = QString::fromStdWString(library->getLibPath());
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
	QString relativePath = QDir::toNativeSeparators(pluginsDir.relativeFilePath(absolutePath));
	if (relativePath.startsWith(QDir::toNativeSeparators("../../")))
		relativePath = absolutePath;

	if (relativePath.contains(" "))
		relativePath = "\"" + relativePath + "\"";
	parameters = "Library " + relativePath;
	if (library->isVST3() && vst3ClassIndex != 0)
		parameters += " ClassIndex " + QString::number(vst3ClassIndex);
	if (outProcMode)
		parameters += " HostId " + hostId;
	if (chunkData != L"")
	{
		parameters += " ChunkData \"" + QString::fromStdWString(chunkData) + "\"";
	}
	else
	{
		for (auto it : paramMap)
		{
			QString name = QString::fromStdWString(it.first);
			if (name.contains(" ") || name.contains("\""))
				name = "\"" + name.replace("\"", "\"\"") + "\"";
			parameters += " " + name + " " + QString("%1").arg(it.second);
		}
	}
}

void VSTPluginFilterGUI::loadPreferences(const QVariantMap& prefs)
{
	autoApplyDialog = prefs.value("autoApplyDialog").toBool();
	if (outProcMode)
	{
		QPalette palette = ui->statusLabel->palette();
		palette.setColor(QPalette::Active, QPalette::WindowText, Qt::black);
		palette.setColor(QPalette::Inactive, QPalette::WindowText, Qt::black);
		ui->statusLabel->setPalette(palette);
		ui->statusLabel->setText(tr("Out-of-process VST host"));
	}
	else
		initPlugin();
}

void VSTPluginFilterGUI::storePreferences(QVariantMap& prefs)
{
	prefs.insert("autoApplyDialog", autoApplyDialog);
}

void VSTPluginFilterGUI::prepareDelete()
{
	appendOutProcDebugLog("prepareDelete outProc=" + QString(outProcMode ? "true" : "false") + " hostId=" + hostId);
	if (outProcMode)
		terminateOutProcPanel();
}

void VSTPluginFilterGUI::on_openPanelButton_clicked()
{
	if (outProcMode)
	{
		openOutProcPanel();
		return;
	}

	initPlugin();

	if (effect != NULL)
	{
		effect->writeToEffect(chunkData, paramMap);

		VSTPluginFilterGUIDialog dialog(this, effect, autoApplyDialog);
		connect(dialog.getApplyButton(), SIGNAL(pressed()), SLOT(applyDialog()));
		connect(dialog.getAutoApplyCheckBox(), SIGNAL(toggled(bool)), SLOT(autoApplyToggled(bool)));
		lastReadTimer.invalidate();
		idleTimer.start();

		if (dialog.exec() == QDialog::Accepted)
		{
			effect->readFromEffect(chunkData, paramMap);
			updateModel();
			updatePermissionWarning();
		}
		idleTimer.stop();
	}
}

void VSTPluginFilterGUI::on_reloadButton_clicked()
{
	if (library->getLibPath().empty())
		return;

	if (outProcMode)
	{
		appendOutProcDebugLog("reload requested hostId=" + hostId);
		terminateOutProcPanel();
		hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		ui->openPanelButton->setText(tr("Open panel"));
		ui->statusLabel->setText(tr("Out-of-process VST host reloading"));
	}
	else
	{
		ui->statusLabel->setText(tr("VST plugin reloading"));
	}

	// Rewriting the row makes the APO audio engine reconstruct its VST instance.
	emit updateModel();
}

void VSTPluginFilterGUI::on_vst3ClassComboBox_currentIndexChanged(int index)
{
	if (index < 0 || index == vst3ClassIndex)
		return;

	vst3ClassIndex = index;
	chunkData = L"";
	paramMap.clear();
	if (outProcMode)
	{
		terminateOutProcPanel();
		hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	}
	else
	{
		releasePluginInstance();
		initPlugin();
	}
	emit updateModel();
	updatePermissionWarning();
}

void VSTPluginFilterGUI::openOutProcPanel()
{
	if (outProcGuiRunning)
	{
		if (outProcGuiHidden)
		{
			signalOutProcPanel(L"GuiShow");
			outProcGuiHidden = false;
			ui->openPanelButton->setText(tr("Hide panel"));
		}
		else
		{
			signalOutProcPanel(L"GuiHide");
			outProcGuiHidden = true;
			ui->openPanelButton->setText(tr("Show panel"));
		}
		return;
	}

	if (signalOutProcPanel(L"GuiShow"))
	{
		outProcGuiRunning = true;
		outProcGuiHidden = false;
		ui->openPanelButton->setText(tr("Hide panel"));
		ui->statusLabel->setText(tr("Out-of-process VST panel is open"));
		idleTimer.start();
		return;
	}

	if (library->getLibPath() == L"")
		return;

	const QString hostExe = "EqApoOutProcHost.exe";
	QString hostPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(hostExe);
	if (!QFile::exists(hostPath))
	{
		QMessageBox::warning(this, tr("VST plugin"), tr("%1 was not found next to Editor.exe.").arg(hostExe));
		return;
	}

	outProcGuiConfigPath = QDir::temp().absoluteFilePath("EqApoVSTGui-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".opvs");
	OutProcVSTConfig config;
	config.libraryPath = library->getLibPath();
	config.vst3ClassIndex = vst3ClassIndex;
	config.chunkData = chunkData;
	config.paramMap = paramMap;
	if (!OutProcWriteVSTConfig(outProcGuiConfigPath.toStdWString(), config))
	{
		QMessageBox::warning(this, tr("VST plugin"), tr("Could not create temporary VST host configuration."));
		outProcGuiConfigPath.clear();
		return;
	}

	QStringList arguments;
	arguments << "--gui" << "--session" << hostId << "--vst-config" << outProcGuiConfigPath;
	appendOutProcDebugLog("open panel hostId=" + hostId + " config=" + outProcGuiConfigPath);

	qint64 pid = 0;
	if (!QProcess::startDetached(hostPath, arguments, QCoreApplication::applicationDirPath(), &pid))
	{
		QMessageBox::warning(this, tr("VST plugin"), tr("Could not start the out-of-process VST host."));
		QFile::remove(outProcGuiConfigPath);
		outProcGuiConfigPath.clear();
		return;
	}

	outProcGuiRunning = true;
	outProcGuiPid = pid;
	outProcGuiHidden = false;
	ui->openPanelButton->setText(tr("Hide panel"));
	ui->statusLabel->setText(tr("Out-of-process VST panel is open"));
	idleTimer.start();
}

bool VSTPluginFilterGUI::signalOutProcPanel(const wchar_t* suffix)
{
	QString objectName = makeOutProcObjectName(hostId, suffix);
	HANDLE eventHandle = OpenEventW(EVENT_MODIFY_STATE, FALSE, reinterpret_cast<LPCWSTR>(objectName.utf16()));
	if (eventHandle == NULL)
	{
		appendOutProcDebugLog("signal " + QString::fromWCharArray(suffix) + " hostId=" + hostId + " open failed gle=" + QString::number(GetLastError()));
		return false;
	}
	const BOOL ok = SetEvent(eventHandle);
	CloseHandle(eventHandle);
	appendOutProcDebugLog("signal " + QString::fromWCharArray(suffix) + " hostId=" + hostId + " ok=" + QString(ok ? "true" : "false") + " gle=" + QString::number(GetLastError()));
	return ok == TRUE;
}

bool VSTPluginFilterGUI::consumeOutProcPanelSignal(const wchar_t* suffix)
{
	QString objectName = makeOutProcObjectName(hostId, suffix);
	HANDLE eventHandle = OpenEventW(SYNCHRONIZE, FALSE, reinterpret_cast<LPCWSTR>(objectName.utf16()));
	if (eventHandle == NULL)
		return false;
	const DWORD waitResult = WaitForSingleObject(eventHandle, 0);
	CloseHandle(eventHandle);
	return waitResult == WAIT_OBJECT_0;
}

static bool terminateOutProcPidForHostId(const QString& hostId)
{
	QString objectName = makeOutProcObjectName(hostId, L"GuiInfo");
	HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, reinterpret_cast<LPCWSTR>(objectName.utf16()));
	if (mapping == NULL)
	{
		appendOutProcDebugLog("pid mapping open failed hostId=" + hostId + " gle=" + QString::number(GetLastError()));
		return false;
	}

	OutProcGuiInfo* info = static_cast<OutProcGuiInfo*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(OutProcGuiInfo)));
	if (info == nullptr)
	{
		CloseHandle(mapping);
		appendOutProcDebugLog("pid mapping view failed hostId=" + hostId + " gle=" + QString::number(GetLastError()));
		return false;
	}

	const DWORD pid = (info->magic == OUTPROC_GUI_INFO_MAGIC && info->version == OUTPROC_GUI_INFO_VERSION) ? info->processId : 0;
	UnmapViewOfFile(info);
	CloseHandle(mapping);

	if (pid == 0 || pid == GetCurrentProcessId())
	{
		appendOutProcDebugLog("pid mapping invalid hostId=" + hostId + " pid=" + QString::number(pid));
		return false;
	}

	HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
	if (process == NULL)
	{
		appendOutProcDebugLog("pid mapping open process failed hostId=" + hostId + " pid=" + QString::number(pid) + " gle=" + QString::number(GetLastError()));
		return false;
	}

	TerminateProcess(process, 0);
	WaitForSingleObject(process, 1000);
	CloseHandle(process);
	appendOutProcDebugLog("pid mapping terminated hostId=" + hostId + " pid=" + QString::number(pid));
	return true;
}

static QString makeOutProcPidPath(const QString& hostId)
{
	QString safeId = hostId;
	for (QChar& ch : safeId)
	{
		const bool ok = ch.isLetterOrNumber() || ch == '-' || ch == '_';
		if (!ok)
			ch = '_';
	}
	return QDir::temp().absoluteFilePath("EqApoOutProcHost-" + safeId + ".pid");
}

static bool terminateOutProcPidFileForHostId(const QString& hostId)
{
	const QString path = makeOutProcPidPath(hostId);
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		appendOutProcDebugLog("pid file open failed hostId=" + hostId + " path=" + path);
		return false;
	}

	bool ok = false;
	const DWORD pid = file.readAll().trimmed().toULong(&ok);
	file.close();
	if (!ok || pid == 0 || pid == GetCurrentProcessId())
	{
		appendOutProcDebugLog("pid file invalid hostId=" + hostId + " path=" + path + " pid=" + QString::number(pid));
		return false;
	}

	HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
	if (process == NULL)
	{
		appendOutProcDebugLog("pid file open process failed hostId=" + hostId + " pid=" + QString::number(pid) + " gle=" + QString::number(GetLastError()));
		return false;
	}

	TerminateProcess(process, 0);
	WaitForSingleObject(process, 1000);
	CloseHandle(process);
	QFile::remove(path);
	appendOutProcDebugLog("pid file terminated hostId=" + hostId + " pid=" + QString::number(pid) + " path=" + path);
	return true;
}

static bool terminateOutProcPid(qint64 pid, const QString& hostId)
{
	if (pid <= 0 || static_cast<DWORD>(pid) == GetCurrentProcessId())
		return false;

	HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
	if (process == NULL)
	{
		appendOutProcDebugLog("detached pid open process failed hostId=" + hostId + " pid=" + QString::number(pid) + " gle=" + QString::number(GetLastError()));
		return false;
	}

	TerminateProcess(process, 0);
	WaitForSingleObject(process, 1000);
	CloseHandle(process);
	appendOutProcDebugLog("detached pid terminated hostId=" + hostId + " pid=" + QString::number(pid));
	return true;
}

void VSTPluginFilterGUI::closeOutProcPanel()
{
	if (!outProcGuiRunning)
		return;

	signalOutProcPanel(L"GuiHide");
	outProcGuiHidden = true;

	if (ui != nullptr)
	{
		ui->openPanelButton->setText(tr("Show panel"));
		ui->statusLabel->setText(tr("Out-of-process VST host"));
	}
}

void VSTPluginFilterGUI::terminateOutProcPanel()
{
	appendOutProcDebugLog("terminate panel hostId=" + hostId + " running=" + QString(outProcGuiRunning ? "true" : "false") + " pid=" + QString::number(outProcGuiPid));
	signalOutProcPanel(L"GuiExit");
	terminateOutProcPidForHostId(hostId);
	terminateOutProcPidFileForHostId(hostId);
	terminateOutProcPid(outProcGuiPid, hostId);

	if (!outProcGuiConfigPath.isEmpty())
	{
		OutProcVSTConfig updatedConfig;
		if (OutProcReadVSTConfig(outProcGuiConfigPath.toStdWString(), updatedConfig))
		{
			chunkData = updatedConfig.chunkData;
			paramMap = updatedConfig.paramMap;
		}
		QFile::remove(outProcGuiConfigPath);
	}
	outProcGuiConfigPath.clear();
	outProcGuiRunning = false;
	outProcGuiPid = 0;
	outProcGuiHidden = false;
}

void VSTPluginFilterGUI::applyDialog()
{
	effect->readFromEffect(chunkData, paramMap);
	updateModel();
	updatePermissionWarning();
}

void VSTPluginFilterGUI::autoApplyToggled(bool checked)
{
	autoApplyDialog = checked;
}

void VSTPluginFilterGUI::initPlugin()
{
	if (effect != NULL)
		return;

	QColor color;
	QString text;
	if (library->getLibPath() == L"")
	{
		text = tr("No file selected.");
		color = Qt::red;
	}
	else
	{
		int result = library->initialize();
		if (result < 0)
		{
			color = Qt::red;

			switch (result)
			{
			case AbstractLibrary::FILE_NOT_FOUND:
				text = tr("File not found.");
				break;
			case AbstractLibrary::LOADING_FAILED:
				text = tr("Library could not be loaded.");
				break;
			case AbstractLibrary::FUNCTIONS_MISSING:
				text = tr("Library does not contain needed functions.");
				break;
			case AbstractLibrary::WRONG_ARCHITECTURE:
#ifdef _WIN64
				int bitDepth = 64;
#else
				int bitDepth = 32;
#endif
				text = tr("Library has the wrong architecture. Only %1-bit libraries are supported.").arg(bitDepth);
				break;
			}
		}
		else
		{
			effect = new VSTPluginInstance(library, 1, vst3ClassIndex);
			if (effect->initialize())
			{
				effect->setLanguage(QLocale().language() == QLocale::German ? 2 : 1);
				effect->setAutomateFunc(bind(&VSTPluginFilterGUI::onAutomate, this));

				color = Qt::black;
				text = QString::fromStdWString(effect->getName());
			}
			else
			{
				delete effect;
				effect = NULL;

				color = Qt::red;
				text = tr("Plugin crashed during initialization.");
			}
		}
	}

	QPalette palette = ui->statusLabel->palette();
	palette.setColor(QPalette::Active, QPalette::WindowText, color);
	palette.setColor(QPalette::Inactive, QPalette::WindowText, color);
	ui->statusLabel->setPalette(palette);
	ui->statusLabel->setText(text);
}

void VSTPluginFilterGUI::releasePluginInstance()
{
	if (effect == NULL)
		return;

	idleTimer.stop();
	effect->setAutomateFunc(nullptr);
	effect->setSizeWindowFunc(nullptr);
	effect->stopEditing();

	if (library->isVST3())
	{
		effect = NULL;
		return;
	}

	delete effect;
	effect = NULL;
}

void VSTPluginFilterGUI::on_pathLineEdit_editingFinished()
{
	if (QString::fromStdWString(library->getLibPath()) != ui->pathLineEdit->text())
	{
		int oldId = 0;
		if (outProcMode)
		{
			terminateOutProcPanel();
			hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		}
		if (effect != NULL)
		{
			oldId = effect->uniqueID();
			releasePluginInstance();
		}

		QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
		QString path = ui->pathLineEdit->text();
		if (path.length() > 0)
			path = QDir::toNativeSeparators(QFileInfo(pluginsDir, ui->pathLineEdit->text()).absoluteFilePath());
		library = VSTPluginLibrary::getInstance(path.toStdWString());
		vst3ClassIndex = 0;
		refreshVST3ClassComboBox();
		if (!outProcMode)
			initPlugin();

		if (outProcMode || effect == NULL || oldId == 0 || effect->uniqueID() != oldId)
		{
			chunkData = L"";
			paramMap.clear();
		}

		updateModel();
		updatePermissionWarning();
	}
}

void VSTPluginFilterGUI::refreshVST3ClassComboBox()
{
	ui->vst3ClassComboBox->blockSignals(true);
	ui->vst3ClassComboBox->clear();
	ui->vst3ClassComboBox->setVisible(false);

	if (library != NULL && library->isVST3() && library->initialize() >= 0 && library->getVST3ClassCount() > 1)
	{
		for (int i = 0; i < library->getVST3ClassCount(); ++i)
			ui->vst3ClassComboBox->addItem(QString::fromUtf8(library->getVST3ClassInfo(i).name));
		if (vst3ClassIndex < 0 || vst3ClassIndex >= library->getVST3ClassCount())
			vst3ClassIndex = 0;
		ui->vst3ClassComboBox->setCurrentIndex(vst3ClassIndex);
		ui->vst3ClassComboBox->setVisible(true);
	}

	ui->vst3ClassComboBox->blockSignals(false);
}

void VSTPluginFilterGUI::on_selectButton_clicked()
{
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));

	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	QString lastDir = settings.value("vst/lastDir", "").toString();
	if (lastDir == "")
		lastDir = pluginsDir.absolutePath();

	QFileInfo fileInfo(lastDir);
	QString path = ui->pathLineEdit->text();
	if (path.length() > 0)
		fileInfo.setFile(pluginsDir, path);

	QFileDialog dialog(this, tr("Select VST plugin"), fileInfo.absoluteFilePath(), "*.dll *.vst3");
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(tr("VST plugins (*.dll *.vst3)"));
	if (path.length() > 0)
		dialog.selectFile(fileInfo.fileName());
	if (dialog.exec() == QDialog::Accepted)
	{
		QString absolutePath = dialog.selectedFiles().first();
		settings.setValue("vst/lastDir", QDir::toNativeSeparators(QFileInfo(absolutePath).absolutePath()));
		QString relativePath = pluginsDir.relativeFilePath(absolutePath);
		if (relativePath.startsWith("../../"))
			relativePath = absolutePath;
		ui->pathLineEdit->setText(QDir::toNativeSeparators(relativePath));
		on_pathLineEdit_editingFinished();
	}
}

void VSTPluginFilterGUI::on_idle()
{
	if (outProcMode && outProcGuiRunning && consumeOutProcPanelSignal(L"GuiHidden"))
	{
		outProcGuiHidden = true;
		ui->openPanelButton->setText(tr("Show panel"));
	}

	if (outProcMode && outProcGuiRunning && !outProcGuiConfigPath.isEmpty())
	{
		if (!lastReadTimer.isValid() || lastReadTimer.elapsed() > 500)
		{
			OutProcVSTConfig updatedConfig;
			if (OutProcReadVSTConfig(outProcGuiConfigPath.toStdWString(), updatedConfig)
				&& (chunkData != updatedConfig.chunkData || paramMap != updatedConfig.paramMap))
			{
				chunkData = updatedConfig.chunkData;
				paramMap = updatedConfig.paramMap;
				updateModel();
			}
			lastReadTimer.restart();
		}
	}

	if (effect != NULL)
	{
		if (autoApplyDialog)
		{
			if (!lastReadTimer.isValid() || lastReadTimer.elapsed() > 1000)
			{
				effect->readFromEffect(chunkData, paramMap);
				updateModel();
				updatePermissionWarning();
				lastReadTimer.restart();
			}
		}
	}
}

void VSTPluginFilterGUI::onAutomate()
{
	if (autoApplyDialog)
	{
		effect->readFromEffect(chunkData, paramMap);
		updateModel();
		updatePermissionWarning();
	}
}

void VSTPluginFilterGUI::onSizeWindow(int w, int h)
{
	Q_UNUSED(w);
	Q_UNUSED(h);
}

void VSTPluginFilterGUI::updatePermissionWarning()
{
	if (effect == NULL)
	{
		ui->warningTextEdit->setVisible(false);
		return;
	}

	ACCESS_MASK mask = GENERIC_READ;
	try
	{
		mask = RegistryHelper::getFileAccessForUser(library->getLibPath(), SECURITY_LOCAL_SERVICE_RID);
	}
	catch (RegistryException e)
	{
		// ignore
	}

	if ((mask & GENERIC_READ) != GENERIC_READ && (mask & FILE_GENERIC_READ) != FILE_GENERIC_READ)
	{
		QString text = tr("The library is not readable by the audio service.\nChange the file permissions or copy the file to the VSTPlugins directory.");

		ui->warningTextEdit->setPlainText(text);
		QSize textSize = ui->warningTextEdit->fontMetrics().size(0, text);
		ui->warningTextEdit->setFixedSize(textSize + GUIHelper::scale(QSize(40, 15)));
		ui->warningTextEdit->setVisible(true);
		return;
	}

	QStringList files;
	if (chunkData != L"" && chunkData.length() < 100000)
	{
		QByteArray bytes = QByteArray::fromBase64(QString::fromStdWString(chunkData).toUtf8());
		QString string = QString::fromUtf8(bytes.data(), bytes.length());
		QRegularExpression regexp("[A-Za-z]:(?:\\\\[\\w \\(\\)-]+)+\\.[A-Za-z]{3}");
		QRegularExpressionMatchIterator it = regexp.globalMatch(string);
		while (it.hasNext())
		{
			QRegularExpressionMatch m = it.next();
			QString path = m.captured();
			QFile file(path);
			if (file.exists())
			{
				ACCESS_MASK mask = GENERIC_READ;
				try
				{
					mask = RegistryHelper::getFileAccessForUser(path.toStdWString(), SECURITY_LOCAL_SERVICE_RID);
				}
				catch (RegistryException e)
				{
					// ignore
				}

				if ((mask & GENERIC_READ) != GENERIC_READ && (mask & FILE_GENERIC_READ) != FILE_GENERIC_READ)
					files.append(path);
			}
		}
	}

	if (files.isEmpty())
	{
		ui->warningTextEdit->setVisible(false);
		ui->warningTextEdit->setPlainText("");
	}
	else
	{
		files.removeDuplicates();
		QString text = tr("The plugin seemingly accesses these files not readable by the audio service:\n"
				"%0\n"
				"Change the file permissions or copy the files to the config directory.").arg(files.join("\n"));
		ui->warningTextEdit->setPlainText(text);
		QSize textSize = ui->warningTextEdit->fontMetrics().size(0, text);
		ui->warningTextEdit->setFixedSize(textSize + GUIHelper::scale(QSize(40, 15)));
		ui->warningTextEdit->setVisible(true);
	}
}
