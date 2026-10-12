#pragma once
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include "Editor/guis/VSTPluginFilterGUI.h"
#include "helpers/StringHelper.h"
#include "outproc/OutProcGuiObjects.h"
#include "outproc/OutProcAudioProtocol.h"

inline int runProtectedCloseAudit(QApplication& app,const QString& plugin,const QString& directory) {
    app.setProperty("engineeringAudit",true);QDir().mkpath(directory);
    VSTPluginFilterGUI row(VSTPluginLibrary::getInstance(plugin.toStdWString()),L"",{});
    QString command,parameters;row.store(command,parameters);std::wstring session;
    QFile initial(QDir(directory).filePath("initial-config.txt"));
    if(initial.open(QIODevice::WriteOnly))initial.write((command+": "+parameters+"\n").toUtf8());initial.close();
    auto tokens=StringHelper::splitQuoted(parameters.toStdWString(),' ');
    for(unsigned i=0;i+1<tokens.size();i+=2)if(tokens[i]==L"HostId")session=tokens[i+1];
    row.show();QElapsedTimer elapsed;elapsed.start();qint64 requested=-1,returned=-1,exited=-1,maxTickGap=0,lastTick=0;int ticks=0;DWORD pid=0,exitCode=STILL_ACTIVE;
    HANDLE child=nullptr;
    QTimer::singleShot(100,&row,[&](){QMetaObject::invokeMethod(&row,"on_openPanelButton_clicked",Qt::DirectConnection);});
    QTimer clock;QObject::connect(&clock,&QTimer::timeout,&row,[&]() {
        ++ticks;const auto now=elapsed.elapsed();maxTickGap=(std::max)(maxTickGap,now-lastTick);lastTick=now;
        if(!child) {
            row.store(command,parameters);
            auto current=StringHelper::splitQuoted(parameters.toStdWString(),' ');
            for(unsigned i=0;i+1<current.size();i+=2)if(current[i]==L"HostId")session=current[i+1];
            HANDLE mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,OutProcGuiObjectName(session,L"GuiInfo").c_str());
            if(mapping) {
                auto* info=static_cast<const OutProcGuiInfo*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(OutProcGuiInfo)));
                if(info && info->magic==OUTPROC_GUI_INFO_MAGIC && info->version==OUTPROC_GUI_INFO_VERSION)pid=info->processId;
                if(info)UnmapViewOfFile(info);CloseHandle(mapping);
                if(pid && pid==row.getOwnedGuiProcessId())child=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
            }
        }
        if(child && requested<0 && now>=2000) {requested=now;row.prepareDelete();returned=elapsed.elapsed();}
        if(child && requested>=0 && WaitForSingleObject(child,0)==WAIT_OBJECT_0) {GetExitCodeProcess(child,&exitCode);exited=elapsed.elapsed();app.quit();}
        if(now>7000)app.quit();
    });clock.start(20);app.exec();clock.stop();if(child)CloseHandle(child);
    const auto closeMs=returned>=requested && requested>=0?returned-requested:-1;
    const bool passed=requested>=0 && exited>=0 && closeMs>=0 && closeMs<200 && ticks>=20 && maxTickGap<250;
    QFile report(QDir(directory).filePath("result.json"));if(!report.open(QIODevice::WriteOnly))return 3;
    report.write(QJsonDocument(QJsonObject{{"status",passed?"PASS":"GUI_FAILURE"},{"plugin",plugin},{"gui_pid",int(pid)},
        {"close_call_ms",closeMs},{"shutdown_elapsed_ms",exited>=0?exited-requested:-1},{"qt_ticks",ticks},
        {"max_qt_tick_gap_ms",maxTickGap},{"gui_exit_code",int(exitCode)},
        {"shutdown_requested",row.getGuiShutdownDiagnostics().requested},
        {"shutdown_completed",row.getGuiShutdownDiagnostics().completed},
        {"shutdown_forced",row.getGuiShutdownDiagnostics().forced},
        {"limits","Actual protected Qt row prepareDelete and owned GUI process exit; fixture markers separately prove graceful lifecycle; no installed audiodg certification"}}).toJson());
    return passed?0:1;
}
