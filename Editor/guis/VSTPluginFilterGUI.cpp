#include "Editor/helpers/UserBrokerStartup.h"
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
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileDialog>
#include <QCoreApplication>
#include <QDialog>
#include <QEventLoop>
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
#include <QPushButton>
#include <QUuid>
#include <cwchar>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "outproc/OutProcAudioProtocol.h"
#include "outproc/OutProcGuiObjects.h"
#include "outproc/OutProcVSTConfig.h"
#include "outproc/OutProcControlEndpoint.h"
#include "helpers/aeffectx.h"
#include "helpers/StringHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/MainWindow.h"
#include "VSTPluginFilterGUI.h"
#include "ui_VSTPluginFilterGUI.h"

using namespace std;

static QString makeOutProcPidPath(const QString& hostId);

static QString makeOutProcObjectName(const QString& hostId, const wchar_t* suffix)
{
    return QString::fromStdWString(OutProcGuiObjectName(hostId.toStdWString(), suffix));
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

VSTPluginFilterGUI::VSTPluginFilterGUI(std::shared_ptr<VSTPluginLibrary> library, const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap, const QString& hostId, int vst3ClassIndex,const std::wstring& classUID)
	: ui(new Ui::VSTPluginFilterGUI), library(library), chunkData(chunkData), paramMap(paramMap), hostId(hostId), vst3ClassIndex(vst3ClassIndex),selectedClassUID(classUID)
{
	ui->setupUi(this);
    ui->pathLineEdit->setPathMode();
	if (!EqApoControl::validSessionId(this->hostId.toStdWString()) || this->hostId=="GuiUserSID")
	{
		this->hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        needsControlIdentitySave=true;
    }
	ui->frame->setVisible(false);
	if (QGridLayout* grid = qobject_cast<QGridLayout*>(layout()))
	{
        auto* diagnostics=new QPushButton(tr("VST compatibility notes…"),this);
        connect(diagnostics,&QPushButton::clicked,this,[this]() {
            const QString notes=tr(
                "The VST module is not universally compatible with all VSTs on the market.\n\n"
                "If you experience:\n\n"
                "- A registered commercial VST asking for the license\n"
                "- Audio popping\n"
                "- Editor.exe crashes and closes\n"
                "- Audio changes you've made in the VST GUI aren't applied\n"
                "- The GUI doesn't open when you click the button\n"
                "- Audio artifacts or delays when processing changes\n\n"
                "Then the VST is partially or completely incompatible with APO's architecture and the technical limitations of the Windows audio engine.");
            QMessageBox dialog(QMessageBox::Information,tr("VST compatibility notes"),notes,QMessageBox::Ok,this);
            dialog.exec();
        });
        grid->addWidget(diagnostics,4,0,1,5);
	}
	ui->warningTextEdit->hide();

	QString absolutePath = QString::fromStdWString(library->getLibPath());
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
	QString relativePath = QDir::toNativeSeparators(pluginsDir.relativeFilePath(absolutePath));
	if (relativePath.startsWith(QDir::toNativeSeparators("../../")))
		relativePath = absolutePath;
	ui->pathLineEdit->setText(relativePath);
	refreshVST3ClassComboBox();
    scanProtectedPlugin();

	connect(&idleTimer, &QTimer::timeout, this, &VSTPluginFilterGUI::on_idle);
	idleTimer.setTimerType(Qt::PreciseTimer);
	idleTimer.setInterval(16);
}

VSTPluginFilterGUI::~VSTPluginFilterGUI()
{
    if(scanner) {scanner->disconnect(this);scanner->kill();scanner->waitForFinished(1000);}
	if(guiProcess || nativeDspPanel)terminateOutProcPanel();else closeOutProcPanel();

	delete ui;
}

void VSTPluginFilterGUI::store(QString& command, QString& parameters)
{
	command = "VSTPlugin";

	QString absolutePath = QString::fromStdWString(library->getLibPath());
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
	QString relativePath = QDir::toNativeSeparators(pluginsDir.relativeFilePath(absolutePath));
	if (relativePath.startsWith(QDir::toNativeSeparators("../../")))
		relativePath = absolutePath;

	if (relativePath.contains(" "))
		relativePath = "\"" + relativePath + "\"";
	parameters = "Library " + relativePath;
	if (library->isVST3()) {
        const auto uid=selectedClassUID.empty()?library->getVST3ClassUID(vst3ClassIndex):selectedClassUID;
        parameters += " ClassIndex " + QString::number(vst3ClassIndex);
        if(!uid.empty())parameters += " ClassUID " + QString::fromStdWString(uid);
    }
    {
        parameters += " HostId " + hostId;
        const auto user=EqApoControl::currentUserSid();
        if(!user.empty())parameters += " GuiUserSID " + QString::fromStdWString(user);
    }
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
    Q_UNUSED(prefs);
    QPalette palette = ui->statusLabel->palette();
    palette.setColor(QPalette::Active, QPalette::WindowText, Qt::black);
    palette.setColor(QPalette::Inactive, QPalette::WindowText, Qt::black);
    ui->statusLabel->setPalette(palette);
}

void VSTPluginFilterGUI::storePreferences(QVariantMap& prefs)
{
    // Older rows retain a compatible preference; live edits now always apply.
    prefs.insert("autoApplyDialog", true);
}

void VSTPluginFilterGUI::prepareDelete()
{
    appendOutProcDebugLog("prepareDelete hostId=" + hostId);
    terminateOutProcPanel();
}

bool VSTPluginFilterGUI::flushPendingEdits()
{
    if(!nativeDspPanel)return !nativeStateSaveFailed;
    // Closing the editor must obtain the worker's final, forced checkpoint.
    // This event loop runs only on the GUI thread and never calls the plugin.
    closeOutProcPanel();
    QEventLoop waiting;QTimer poll,deadline;
    poll.setInterval(16);deadline.setSingleShot(true);
    connect(&poll,&QTimer::timeout,&waiting,[this,&waiting](){on_idle();if(!nativeDspPanel)waiting.quit();});
    connect(&deadline,&QTimer::timeout,&waiting,&QEventLoop::quit);
    poll.start();deadline.start(1500);waiting.exec();
    if(nativeDspPanel){ui->statusLabel->setText(tr("Waiting for the VST to save its final state"));return false;}
    return !nativeStateSaveFailed;
}

void VSTPluginFilterGUI::on_openPanelButton_clicked()
{
    const auto broker=ensureSelectedUserBroker();
    if(broker.configured && broker.ordinaryUser)nativeEndpointRetries=100;
    openOutProcPanel();
}

void VSTPluginFilterGUI::on_reloadButton_clicked()
{
    if (library->getLibPath().empty())return;
    appendOutProcDebugLog("reload requested hostId=" + hostId);
    terminateOutProcPanel();
    hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ui->openPanelButton->setText(tr("Open panel"));
    ui->statusLabel->setText(tr("VST plugin reloading"));
    // An explicit reload requests a fresh instance, including recovery from a
    // failed plugin. Ordinary bypass/reactivation retains the existing instance.
    emit updateModel();
}

void VSTPluginFilterGUI::on_vst3ClassComboBox_currentIndexChanged(int index)
{
    if (index < 0 || index == vst3ClassIndex)return;
    terminateOutProcPanel();
    vst3ClassIndex = index;
    selectedClassUID=library->getVST3ClassUID(index);
    chunkData.clear();paramMap.clear();
    hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    emit updateModel();
}

EqApoControl::NativeViewSnapshot VSTPluginFilterGUI::nativePanelControl(unsigned command,bool consume)
{
    return EqApoControl::nativeViewControl(hostId.toStdWString(),
        EqApoControl::controlIdentity(library->getLibPath(),selectedClassUID,vst3ClassIndex),
        command,consume,nativePanelGeneration);
}

void VSTPluginFilterGUI::openOutProcPanel()
{
    if(scanner && library->isVST3() && selectedClassUID.empty()) {
        pendingPanelOpen=true;
        ui->statusLabel->setText(tr("Checking plugin before opening its panel"));
        return;
    }
    if(needsControlIdentitySave){needsControlIdentitySave=false;nativeEndpointRetries=100;emit updateModel();}
    if(!guiProcess) {
        const auto view=nativePanelControl();
        if(view.available) {
            const bool hide=nativeDspPanel && view.status==2;
            if(nativePanelControl(hide?2:1).available) {
                nativeEndpointRetries=0;pendingPanelOpen=false;nativeDspPanel=true;nativeViewOpening=!hide;nativeClosePending=false;nativePanelGeneration=view.generation;outProcGuiRunning=true;outProcGuiHidden=hide;
                ui->openPanelButton->setText(hide?tr("Show panel"):tr("Hide panel"));
                ui->statusLabel->setText(tr("Native panel connected to the audio processor"));
                idleTimer.start();return;
            }
        }
    }
    if(!guiProcess && nativeEndpointRetries) {
        --nativeEndpointRetries;pendingPanelOpen=true;
        QTimer::singleShot(50,this,[this](){if(pendingPanelOpen){pendingPanelOpen=false;openOutProcPanel();}});
        return;
    }
    if(lastGuiProcess && lastGuiProcess->shutdown().requested && !lastGuiProcess->shutdown().completed) {
        ui->statusLabel->setText(tr("Previous VST panel is still closing"));
        return;
    }
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
		ui->statusLabel->setText(tr("VST panel"));
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
    config.classUID=selectedClassUID.empty()?library->getVST3ClassUID(vst3ClassIndex):selectedClassUID;
	config.chunkData = chunkData;
	config.paramMap = paramMap;
	if (!OutProcWriteVSTConfig(outProcGuiConfigPath.toStdWString(), config))
	{
		QMessageBox::warning(this, tr("VST plugin"), tr("Could not create temporary VST host configuration."));
		outProcGuiConfigPath.clear();
		return;
	}

	QStringList arguments;
	arguments << "--parent-pid" << QString::number(GetCurrentProcessId()) << "--gui" << "--session" << hostId << "--vst-config" << outProcGuiConfigPath;
	appendOutProcDebugLog("open panel hostId=" + hostId + " config=" + outProcGuiConfigPath);

    std::vector<std::wstring> nativeArguments;
    for(const auto& argument:arguments)nativeArguments.push_back(argument.toStdWString());
    guiProcess=ManagedGuiProcess::launch(hostPath.toStdWString(),nativeArguments,QCoreApplication::applicationDirPath().toStdWString(),
        hostId.toStdWString(),outProcGuiConfigPath.toStdWString(),makeOutProcPidPath(hostId).toStdWString());
	if (!guiProcess)
	{
		QMessageBox::warning(this, tr("VST plugin"), tr("Could not start the VST host."));
		QFile::remove(outProcGuiConfigPath);
		outProcGuiConfigPath.clear();
		return;
	}

	outProcGuiRunning = true;
	outProcGuiPid = guiProcess->processId();
	outProcGuiHidden = false;
	ui->openPanelButton->setText(tr("Hide panel"));
	ui->statusLabel->setText(tr("VST panel"));
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
	if (std::wcscmp(suffix, L"GuiShow") == 0 && guiProcess && guiProcess->alive())
        AllowSetForegroundWindow(guiProcess->processId());
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

void VSTPluginFilterGUI::closeOutProcPanel()
{
    if(nativeDspPanel){
        nativeClosePending=true;nativeViewOpening=false;
        nativePanelControl(3);
        outProcGuiRunning=false;idleTimer.start();return;
    }
	if (!outProcGuiRunning)
		return;

	signalOutProcPanel(L"GuiHide");
	outProcGuiHidden = true;

	if (ui != nullptr)
	{
		ui->openPanelButton->setText(tr("Show panel"));
		ui->statusLabel->setText(tr("VST DSP: Windows user account (outside audiodg)"));
	}
}

void VSTPluginFilterGUI::terminateOutProcPanel()
{
    pendingPanelOpen=false;
    if(nativeDspPanel)closeOutProcPanel();
	appendOutProcDebugLog("terminate panel hostId=" + hostId + " running=" + QString(outProcGuiRunning ? "true" : "false") + " pid=" + QString::number(outProcGuiPid));

	if (!outProcGuiConfigPath.isEmpty())
	{
		OutProcVSTConfig updatedConfig;
		if (OutProcReadVSTConfig(outProcGuiConfigPath.toStdWString(), updatedConfig))
		{
			chunkData = updatedConfig.chunkData;
			paramMap = updatedConfig.paramMap;
		}
	}
    if(guiProcess) {
        lastGuiProcess=guiProcess;
        guiProcess->requestClose();
        guiProcess.reset();
    } else if(outProcGuiRunning) {
        // Reused GUI endpoints are not owned by this row: never terminate
        // a process reconstructed from a PID mapping or file.
        signalOutProcPanel(L"GuiHide");
    }
	outProcGuiConfigPath.clear();
	outProcGuiRunning = false;
	outProcGuiPid = 0;
	outProcGuiHidden = false;
}

void VSTPluginFilterGUI::on_pathLineEdit_editingFinished()
{
    if (QString::fromStdWString(library->getLibPath()) == ui->pathLineEdit->text())return;
    terminateOutProcPanel();
    hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
    QString path = ui->pathLineEdit->text();
    if (!path.isEmpty())path = QDir::toNativeSeparators(QFileInfo(pluginsDir,path).absoluteFilePath());
    library = VSTPluginLibrary::getInstance(path.toStdWString());
    vst3ClassIndex = 0;selectedClassUID.clear();chunkData.clear();paramMap.clear();
    refreshVST3ClassComboBox();scanProtectedPlugin();
    emit updateModel();
}

void VSTPluginFilterGUI::refreshVST3ClassComboBox()
{
	ui->vst3ClassComboBox->blockSignals(true);
	ui->vst3ClassComboBox->clear();
	ui->vst3ClassComboBox->setVisible(false);

	if (library != NULL && library->isVST3() && library->getVST3ClassCount() > 1)
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
    if(nativeDspPanel){
        const auto view=nativePanelControl(0,true);
        if(view.available && view.generation!=nativePanelGeneration){
            nativePanelGeneration=view.generation;
            if(nativeClosePending)nativePanelControl(3);
        }
        for(const auto& edit:view.edits)paramMap[L"#"+std::to_wstring(edit.id)]=float(edit.normalized);
        if(view.snapshotSequence && (view.generation!=nativeSnapshotGeneration || view.snapshotSequence!=nativeSnapshotSequence)) {
            OutProcVSTConfig saved;
            const auto expected=EqApoControl::controlIdentity(library->getLibPath(),selectedClassUID,vst3ClassIndex);
            if(OutProcReadVSTConfig(view.snapshotPath,saved) && EqApoControl::controlIdentity(saved.libraryPath,saved.classUID,saved.vst3ClassIndex)==expected) {
                const bool changed=chunkData!=saved.chunkData || paramMap!=saved.paramMap;
                chunkData=std::move(saved.chunkData);paramMap=std::move(saved.paramMap);
                nativeSnapshotGeneration=view.generation;nativeSnapshotSequence=view.snapshotSequence;
                // The native view edits the audible component itself. Writing
                // config.txt here would replace that component for every edit.
                // Keep its complete checkpoint and commit when the view closes.
                nativeStatePending |= changed;
            }
        }
        if(view.snapshotError){nativeStateSaveFailed=true;ui->statusLabel->setText(tr("Could not save the VST state"));}
        else if(view.snapshotSequence)nativeStateSaveFailed=false;
        if(view.status==2 || view.status==3)nativeViewOpening=false;
        if((!view.available && !nativeViewOpening) || (view.status==1 && (!nativeViewOpening || view.visibility==0)) || view.status==6){
            nativePanelControl(3);
            nativeDspPanel=false;outProcGuiRunning=false;outProcGuiHidden=false;nativeClosePending=false;
            ui->openPanelButton->setText(tr("Open panel"));
            if(nativeStatePending && !nativeStateSaveFailed){nativeStatePending=false;updateModel();}
        } else {outProcGuiHidden=view.visibility==2 || view.status==3;ui->openPanelButton->setText(outProcGuiHidden?tr("Show panel"):tr("Hide panel"));}
        return;
    }
    if(guiProcess && !guiProcess->alive()) {
        terminateOutProcPanel();
        ui->openPanelButton->setText(tr("Open panel"));
        ui->statusLabel->setText(tr("VST panel closed"));
    }
    std::uint64_t generation=lastStateGeneration;
    if(!hostId.isEmpty()) {
        HANDLE map=OpenFileMappingW(FILE_MAP_READ,FALSE,reinterpret_cast<const wchar_t*>(makeOutProcObjectName(hostId,L"GuiInfo").utf16()));
        if(map) {
            auto* info=static_cast<const OutProcGuiInfo*>(MapViewOfFile(map,FILE_MAP_READ,0,0,sizeof(OutProcGuiInfo)));
            if(info && info->magic==OUTPROC_GUI_INFO_MAGIC && info->version==OUTPROC_GUI_INFO_VERSION)
                generation=info->stateGeneration.load(std::memory_order_acquire);
            if(info)UnmapViewOfFile(info);CloseHandle(map);
        }
    }

	if (outProcGuiRunning && consumeOutProcPanelSignal(L"GuiHidden"))
	{
		outProcGuiHidden = true;
		ui->openPanelButton->setText(tr("Show panel"));
	}

	if (outProcGuiRunning && !outProcGuiConfigPath.isEmpty())
	{
		if (generation!=lastStateGeneration)
		{
			OutProcVSTConfig updatedConfig;
			if (OutProcReadVSTConfig(outProcGuiConfigPath.toStdWString(), updatedConfig)
				&& (chunkData != updatedConfig.chunkData || paramMap != updatedConfig.paramMap))
			{
				chunkData = updatedConfig.chunkData;
				paramMap = updatedConfig.paramMap;
				updateModel();
			}
			lastStateGeneration=generation;
			lastReadTimer.restart();
		}
	}

}

void VSTPluginFilterGUI::scanProtectedPlugin() {
    if(scanner || library->getLibPath().empty())return;
    const auto scanLibrary=library;
    ui->warningTextEdit->hide();
    const QString report=QDir::temp().absoluteFilePath("EqApoScan-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".json");
    scanner=new QProcess(this);
    auto* process=scanner;
    process->setProcessChannelMode(QProcess::MergedChannels);
    ui->statusLabel->setText(tr("Checking plugin…"));
    connect(process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this,process,report,scanLibrary](int code,QProcess::ExitStatus status) {
        if(library==scanLibrary) {
            QFile file(report);QJsonObject object;
            if(file.open(QIODevice::ReadOnly))object=QJsonDocument::fromJson(file.readAll()).object();
            if(status==QProcess::NormalExit && code==0 && object["status"]=="PASS") {
                std::vector<Steinberg::PClassInfo> classes;
                for(const auto value:object["classes"].toArray()) {
                    const auto item=value.toObject();Steinberg::FUID id;
                    if(!id.fromString(item["uid"].toString().toLatin1().constData()))continue;
                    Steinberg::PClassInfo info{};id.toTUID(info.cid);
                    strcpy_s(info.category,kVstAudioEffectClass);strncpy_s(info.name,item["name"].toString().toUtf8().constData(),_TRUNCATE);
                    classes.push_back(info);
                }
                scanLibrary->setScannedClassMetadata(classes);
                if(!selectedClassUID.empty())vst3ClassIndex=scanLibrary->findVST3ClassUID(selectedClassUID);
                if(scanLibrary->isVST3() && (vst3ClassIndex<0 || vst3ClassIndex>=scanLibrary->getVST3ClassCount()))
                    ui->statusLabel->setText(tr("Requested plugin class is unavailable"));
                else {
                    if(scanLibrary->isVST3()) {
                        const auto resolved=scanLibrary->getVST3ClassUID(vst3ClassIndex);
                        if(selectedClassUID!=resolved) {
                            selectedClassUID=resolved;nativeEndpointRetries=100;
                            // Initial index identity and resolved UID identity
                            // must not share an endpoint with an old graph.
                            hostId=QUuid::createUuid().toString(QUuid::WithoutBraces);
                            emit updateModel();
                        }
                    }
                    refreshVST3ClassComboBox();ui->statusLabel->setText(tr("Ready · protected processing"));
                }
            } else {
                QString reason;
                if(object["status"]=="FAIL") {
                    switch(object["load_error"].toInt()) {
                    case AbstractLibrary::FILE_NOT_FOUND: reason=tr("Plugin file not found");break;
                    case AbstractLibrary::WRONG_ARCHITECTURE: reason=tr("Plugin architecture does not match the 64-bit host");break;
                    case AbstractLibrary::FUNCTIONS_MISSING: reason=tr("Plugin entry point or audio effect class is missing");break;
                    default: reason=tr("Plugin library could not be loaded");break;
                    }
                    const int error=object["win32_error"].toInt();
                    if(error)reason+=tr(" (Windows error %1: %2)").arg(error).arg(object["message"].toString().trimmed());
                } else if(status==QProcess::CrashExit)reason=tr("Plugin checker crashed or timed out");
                else reason=tr("Plugin checker failed (exit code %1)").arg(code);
                const QString path=QString::fromStdWString(scanLibrary->getLoadPath());
                ui->statusLabel->setText(reason);
                ui->warningTextEdit->setPlainText(reason+"\n"+QDir::toNativeSeparators(path));
                ui->warningTextEdit->show();
            }
        }
        QFile::remove(report);scanner=nullptr;process->deleteLater();
        if(library!=scanLibrary)scanProtectedPlugin();
        else if(pendingPanelOpen) {pendingPanelOpen=false;QTimer::singleShot(0,this,[this](){openOutProcPanel();});}
    });
    connect(process,&QProcess::errorOccurred,this,[this,process](QProcess::ProcessError error) {
        if(error==QProcess::FailedToStart) {ui->statusLabel->setText(tr("Plugin checker could not start"));scanner=nullptr;process->deleteLater();
            if(pendingPanelOpen){pendingPanelOpen=false;QTimer::singleShot(0,this,[this](){openOutProcPanel();});}}
    });
    process->start(QDir(QCoreApplication::applicationDirPath()).filePath("EqApoOutProcHost.exe"),
        {"--parent-pid",QString::number(GetCurrentProcessId()),"--scan","--plugin",QString::fromStdWString(library->getLibPath()),"--result",report});
    QTimer::singleShot(5000,process,[process]() {if(process->state()!=QProcess::NotRunning)process->kill();});
}
