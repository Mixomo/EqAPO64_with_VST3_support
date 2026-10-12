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

#include <QTranslator>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QCommandLineParser>
#include <QSettings>
#include <QStyleHints>
#include <windows.h>
#include "CustomStyle.h"
#include "MainWindow.h"
#include "helpers/UIAudit.h"
#include "helpers/CommercialGuiAudit.h"
#include "helpers/UserBrokerStartup.h"
#include "helpers/RegistryHelper.h"
#include "Editor/helpers/GUIHelper.h"

using namespace std;

static QString getExecutableDir()
{
	wchar_t path[MAX_PATH] = {};
	DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return QDir::currentPath();

	return QFileInfo(QString::fromWCharArray(path, length)).absolutePath();
}

int main(int argc, char* argv[])
{
	int result = -1;
#ifdef _DEBUG
	// _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
	// _CrtSetBreakAlloc(3318);
#endif

	const QString executableDir = getExecutableDir();
	const QString qtPluginDir = QDir(executableDir).filePath("qt");
	QCoreApplication::addLibraryPath(qtPluginDir);
	qputenv("QT_PLUGIN_PATH", QDir::toNativeSeparators(qtPluginDir).toLocal8Bit());
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	bool restart;
	do
	{
		QApplication application(argc, argv);
		if(GUIHelper::isDarkMode())
			application.setStyle("fusion");
		application.setStyle(new CustomStyle(application.style()));

        const auto auditIndex=application.arguments().indexOf("--ui-audit");
        const auto pathAuditIndex=application.arguments().indexOf("--path-width-audit");
        const auto irAuditIndex=application.arguments().indexOf("--bundled-ir-audit");
        const auto pluginDialogIndex=application.arguments().indexOf("--plugin-dialog-audit");
        const auto controlIdentityIndex=application.arguments().indexOf("--control-identity-audit");
        const bool commercialGeneration=application.arguments().contains("--engine-generation-proq-audit");
        const auto generationIndex=application.arguments().indexOf(commercialGeneration?"--engine-generation-proq-audit":"--engine-generation-live-audit");
        if(generationIndex>=0) {
            if(application.arguments().size()<generationIndex+3)return 2;
            const auto plugin=application.arguments()[generationIndex+1],directory=application.arguments()[generationIndex+2];
            const auto name=QFileInfo(plugin).fileName();
            if(commercialGeneration?(name!="FabFilter Pro-Q 3.dll" && name!="FabFilter Pro-Q 4.vst3"):name!="EqApoFixture.vst3")return 2;
            application.setProperty("engineeringAudit",true);QDir().mkpath(directory);
            std::wstring executable=L"Editor",library=plugin.toStdWString(),folder=directory.toStdWString();
            std::wstring report=QDir(directory).filePath("result.json").toStdWString(),mode=commercialGeneration?L"--proq-native-view":L"--native-view";
            wchar_t* args[]={executable.data(),library.data(),folder.data(),report.data(),mode.data()};
            return runEngineGenerationLiveAudit(5,args,[&application](){application.processEvents();});
        }
        const auto protectedLiveIndex=application.arguments().indexOf("--protected-row-live-audit");
        if(protectedLiveIndex>=0){if(application.arguments().size()<protectedLiveIndex+3)return 2;return runProtectedRowLiveAudit(application,application.arguments()[protectedLiveIndex+1],application.arguments()[protectedLiveIndex+2]);}
        const auto saveAuditIndex=application.arguments().indexOf("--configuration-save-audit");
        const auto commercialIndex=application.arguments().indexOf("--commercial-gui-audit");
        if(commercialIndex>=0){if(application.arguments().size()<commercialIndex+5)return 2;return runCommercialGuiAudit(application,application.arguments()[commercialIndex+1],application.arguments()[commercialIndex+2],application.arguments()[commercialIndex+3]=="OUTPROC",application.arguments()[commercialIndex+4].toUInt());}
        if(saveAuditIndex>=0){if(application.arguments().size()<saveAuditIndex+2)return 2;return runConfigurationSaveAudit(application,application.arguments()[saveAuditIndex+1]);}
        const auto rowLiveIndex=application.arguments().indexOf("--row-live-audit");
        const auto protectedCloseIndex=application.arguments().indexOf("--protected-close-audit");
        const auto rowReleaseIndex=application.arguments().indexOf("--row-release-audit");
        if(rowReleaseIndex>=0) {
            if(application.arguments().size()<rowReleaseIndex+3)return 2;
            return runRowReleaseAudit(application,application.arguments()[rowReleaseIndex+1],application.arguments()[rowReleaseIndex+2]);
        }
        if(protectedCloseIndex>=0) {
            if(application.arguments().size()<protectedCloseIndex+3)return 2;
            return runProtectedCloseAudit(application,application.arguments()[protectedCloseIndex+1],application.arguments()[protectedCloseIndex+2]);
        }
        if(rowLiveIndex>=0) {
            if(application.arguments().size()<rowLiveIndex+6)return 2;
            return runRowLiveAudit(application,application.arguments()[rowLiveIndex+1],application.arguments()[rowLiveIndex+2],
                application.arguments()[rowLiveIndex+3].toUInt(),application.arguments()[rowLiveIndex+4].toDouble(),application.arguments()[rowLiveIndex+5].toDouble());
        }
        if(controlIdentityIndex>=0) {
            if(application.arguments().size()<controlIdentityIndex+3)return 2;
            return runControlIdentityAudit(application,application.arguments()[controlIdentityIndex+1],application.arguments()[controlIdentityIndex+2]);
        }
        if(pluginDialogIndex>=0) {
            if(application.arguments().size()<pluginDialogIndex+3)return 2;
            return runPluginDialogAudit(application,application.arguments()[pluginDialogIndex+1],application.arguments()[pluginDialogIndex+2]);
        }
        if(irAuditIndex>=0) {
            if(application.arguments().size()<irAuditIndex+2)return 2;
            return runBundledIrAudit(application,application.arguments()[irAuditIndex+1]);
        }
        if(pathAuditIndex>=0) {
            if(application.arguments().size()<pathAuditIndex+2)return 2;
            return runPathWidthAudit(application,application.arguments()[pathAuditIndex+1]);
        }
        if(auditIndex>=0) {
            if(application.arguments().size()<auditIndex+3)return 2;
            return runEditorUIAudit(application,application.arguments()[auditIndex+1],application.arguments()[auditIndex+2]);
        }
        const auto brokerAuditIndex=application.arguments().indexOf("--user-broker-startup-audit");
        if(brokerAuditIndex>=0){
            if(application.arguments().size()<brokerAuditIndex+2)return 2;
            const auto startup=ensureSelectedUserBroker();QElapsedTimer elapsed;elapsed.start();
            while(!isUserBrokerRunning() && elapsed.elapsed()<3000){application.processEvents();Sleep(20);}
            const bool running=isUserBrokerRunning();QSaveFile report(application.arguments()[brokerAuditIndex+1]);if(!report.open(QIODevice::WriteOnly))return 3;
            report.write(QJsonDocument(QJsonObject{{"status",startup.configured && startup.ordinaryUser && running?"PASS":"UNAVAILABLE"},
                {"configured",startup.configured},{"ordinary_user",startup.ordinaryUser},{"already_running",startup.alreadyRunning},
                {"started",startup.started},{"pid",double(startup.pid)},{"running",running},{"error",startup.error}}).toJson());report.commit();return running?0:1;
        }
		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);

		QVariant languageValue = settings.value("language");
		if (languageValue.isValid())
			QLocale::setDefault(QLocale(languageValue.toString()));
		else
			QLocale::setDefault(QLocale::system());

		QTranslator qtTranslator;
		if (qtTranslator.load(QLocale(), ":/translations/qtbase", "_"))
			application.installTranslator(&qtTranslator);

		QTranslator editorTranslator;
		if (editorTranslator.load(QLocale(), ":/translations/Editor", "_"))
			application.installTranslator(&editorTranslator);

		QString configPath = QDir::currentPath();
		if (RegistryHelper::keyExists(APP_REGPATH) && RegistryHelper::valueExists(APP_REGPATH, L"ConfigPath"))
			configPath = QString::fromStdWString(RegistryHelper::readValue(APP_REGPATH, L"ConfigPath"));
		QDir configDir(configPath);

		if (!RegistryHelper::keyExists(USER_REGPATH))
			RegistryHelper::createKey(USER_REGPATH);

		if (!RegistryHelper::keyExists(EDITOR_REGPATH))
			RegistryHelper::createKey(EDITOR_REGPATH);

		if (!RegistryHelper::keyExists(EDITOR_PER_FILE_REGPATH))
			RegistryHelper::createKey(EDITOR_PER_FILE_REGPATH);

        ensureSelectedUserBroker();
		MainWindow w(configDir);
		w.show();

		QCommandLineParser parser;
		parser.process(application);
		QStringList args = parser.positionalArguments();
		if (args.isEmpty() && w.isEmpty())
			args = QStringList("config.txt");

		for (const QString& arg : args)
			w.load(configDir.absoluteFilePath(arg));

		w.doChecks();

		result = application.exec();

		restart = w.shouldRestart();
	}
	while (restart);

	return result;
}
