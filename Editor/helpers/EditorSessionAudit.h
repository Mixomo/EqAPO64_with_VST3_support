#pragma once
#include "Editor/MainWindow.h"
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>

inline int runEditorSessionAudit(QApplication& app,const QString& path,const QString& directory){
    app.setProperty("engineeringAudit",true);QDir().mkpath(directory);
    MainWindow window(QFileInfo(path).absoluteDir());window.load(path);window.show();
    struct CloseObserver:QObject{unsigned count=0;bool eventFilter(QObject*,QEvent* event)override{if(event->type()==QEvent::Close)++count;return false;}} observer;
    window.installEventFilter(&observer);
    QTimer closeRequests;closeRequests.setInterval(20);
    QObject::connect(&closeRequests,&QTimer::timeout,&window,[&](){const auto request=QDir(directory).filePath("close.request");if(QFile::exists(request)){QFile::remove(request);window.close();}});closeRequests.start();
    QTimer::singleShot(0,&window,[directory](){QFile ready(QDir(directory).filePath("ready.json"));if(ready.open(QIODevice::WriteOnly))ready.write(QJsonDocument(QJsonObject{{"pid",int(GetCurrentProcessId())}}).toJson());});
    QTimer::singleShot(30000,&window,&QWidget::close);
    const int result=app.exec();QJsonArray rows;
    for(auto* row:window.findChildren<VSTPluginFilterGUI*>()){QString command,parameters;row->store(command,parameters);const auto state=row->getPersistenceAudit();rows.append(QJsonObject{{"line",command+": "+parameters},{"panel",state.panel},{"pending",state.pending},{"failed",state.failed},{"sequence",double(state.sequence)}});}
    QFile report(QDir(directory).filePath("closed-model.json"));if(report.open(QIODevice::WriteOnly))report.write(QJsonDocument(rows).toJson());
    QFile lifecycle(QDir(directory).filePath("lifecycle.json"));if(lifecycle.open(QIODevice::WriteOnly))lifecycle.write(QJsonDocument(QJsonObject{{"close_events",int(observer.count)},{"visible_after_event_loop",window.isVisible()}}).toJson());
    return result;
}

inline int runSaveOnCloseAudit(QApplication& app,const QString& directory){
    app.setProperty("engineeringAudit",true);QDir().mkpath(directory);const auto path=QDir(directory).filePath("config.txt");
    if(QFile::exists(path))return 2;
    {QFile file(path);if(!file.open(QIODevice::WriteOnly))return 3;file.write("Preamp: 0 dB\r\n");}
    MainWindow window{QDir(directory)};window.load(path);window.show();
    auto* module=window.findChild<QWidget*>("PreampFilterGUI");auto* gain=module?module->findChild<QDoubleSpinBox*>("doubleSpinBox"):nullptr;if(!gain)return 4;
    HANDLE reader=CreateFileW(path.toStdWString().c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);if(reader==INVALID_HANDLE_VALUE)return 5;
    bool released=false;gain->setValue(-6);
    QTimer::singleShot(70,&app,[&](){CloseHandle(reader);released=true;});
    QTimer::singleShot(20,&window,&QWidget::close);
    app.exec();if(!released)CloseHandle(reader);
    QFile file(path);const bool opened=file.open(QIODevice::ReadOnly);const auto text=opened?file.readAll():QByteArray();
    const bool passed=text.contains("Preamp: -6 dB");QFile report(QDir(directory).filePath("result.json"));if(report.open(QIODevice::WriteOnly))report.write(QJsonDocument(QJsonObject{{"status",passed?"PASS":"FAIL"},{"save_completed_before_close",passed},{"limits","Actual MainWindow close while own temporary configuration is exclusively locked; no installed configuration changes"}}).toJson());
    return passed?0:1;
}
