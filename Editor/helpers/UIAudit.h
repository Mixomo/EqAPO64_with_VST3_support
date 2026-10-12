#pragma once
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QPixmap>
#include <QAbstractSpinBox>
#include <QAbstractSlider>
#include <QMouseEvent>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QScrollArea>
#include <QScrollBar>
#include <cstdlib>
#include <QSignalBlocker>
#include <memory>
#include <vector>
#include <dbghelp.h>
#include <cstdio>
#include <QLineEdit>
#include <QVBoxLayout>
#include "Editor/guis/VSTPluginFilterGUI.h"
#include "Editor/FilterTable.h"
#include "Editor/guis/ConvolutionFilterGUI.h"
#include <QMenu>
#include "Editor/guis/VSTPluginFilterGUIDialog.h"
#include <QElapsedTimer>
#include "Editor/guis/VSTPluginFilterGUIFactory.h"
#include "helpers/StringHelper.h"
#include "outproc/OutProcControlEndpoint.h"
#include "RowLiveAudit.h"
#include "ProtectedRowLiveAudit.h"
#include "outproc/EngineGenerationLiveAudit.h"
#include "ProtectedCloseAudit.h"

inline int runControlIdentityAudit(QApplication& app,const QString& plugin,const QString& directory) {
    app.setProperty("engineeringAudit",true);QDir().mkpath(directory);
    QJsonArray records;int failures=0;
    for(const QString& alias:{QString("VSTPlugin"),QString("OutProcVSTPlugin")}) {
        auto library=VSTPluginLibrary::getInstance(plugin.toStdWString());
        const auto before=VSTPluginInstance::getLiveInstanceCount();
        VSTPluginFilterGUI original(library,alias=="OutProcVSTPlugin"?L"AACAPg==":L"",{{L"#0",.25f}});
        original.loadPreferences(QVariantMap{{"autoApplyDialog",false}});
        QVariantMap preferences;original.storePreferences(preferences);
        QString command,parameters;original.store(command,parameters);
        auto tokens=StringHelper::splitQuoted(parameters.toStdWString(),' ');
        std::wstring id,sid;
        for(unsigned i=0;i+1<tokens.size();i+=2) {
            if(tokens[i]==L"HostId")id=tokens[i+1];
            if(tokens[i]==L"GuiUserSID")sid=tokens[i+1];
        }
        VSTPluginFilterGUIFactory factory;
        QString inputCommand=alias;
        std::unique_ptr<IFilterGUI> restored(factory.createFilterGUI(inputCommand,parameters));
        QString command2,parameters2;if(restored)restored->store(command2,parameters2);
        const bool passed=EqApoControl::validSessionId(id) && !sid.empty() &&
            sid==EqApoControl::currentUserSid() && command=="VSTPlugin" && command2==command && parameters2==parameters
            && VSTPluginInstance::getLiveInstanceCount()==before
            && GetModuleHandleW(QFileInfo(plugin).fileName().toStdWString().c_str())==nullptr
            && preferences.value("autoApplyDialog").toBool();
        if(!passed)++failures;
        records.append(QJsonObject{{"input_alias",alias},
            {"status",passed?"PASS":"FAIL"},{"command",command},{"parameters",parameters},
            {"restored",parameters2},{"host_id",QString::fromStdWString(id)},
            {"gui_user_sid_present",!sid.empty()},
            {"plugin_not_loaded_in_editor",GetModuleHandleW(QFileInfo(plugin).fileName().toStdWString().c_str())==nullptr},
            {"automatic_apply",preferences.value("autoApplyDialog").toBool()}});
    }
    QFile report(QDir(directory).filePath("result.json"));if(!report.open(QIODevice::WriteOnly))return 3;
    report.write(QJsonDocument(QJsonObject{{"status",failures?"FAIL":"PASS"},{"cases",records},
        {"limits","Actual Qt row serialization and factory reconstruction; no GUI parameter PCM or installed audiodg certification"}}).toJson());
    return failures?1:0;
}

inline int runRowReleaseAudit(QApplication& app,const QString& plugin,const QString& directory) {
    app.setProperty("engineeringAudit",true);QDir().mkpath(directory);
    const auto baseline=VSTPluginInstance::getLiveInstanceCount();QJsonArray cycles;bool passed=true;
    auto library=VSTPluginLibrary::getInstance(plugin.toStdWString());if(library->initialize()<0)return 3;
    QTimer::singleShot(20000,&app,[](){std::exit(9);});
    for(int cycle=0;cycle<5;++cycle) {
        QElapsedTimer timer;timer.start();std::uint64_t during=0;
        {
            VSTPluginFilterGUI row(library,L"",{});row.loadPreferences(QVariantMap{{"autoApplyDialog",false}});
            during=VSTPluginInstance::getLiveInstanceCount();
            QTimer::singleShot(400,&row,[](){
                for(auto* top:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<VSTPluginFilterGUIDialog*>(top))
                    if(dialog->isVisible())dialog->accept();
            });
            QMetaObject::invokeMethod(&row,"on_openPanelButton_clicked",Qt::DirectConnection);
            row.prepareDelete();
        }
        const auto after=VSTPluginInstance::getLiveInstanceCount();
        passed &= during==baseline && after==baseline;
        cycles.append(QJsonObject{{"cycle",cycle},{"during",int(during)},{"after",int(after)},{"elapsed_ms",timer.elapsed()}});
    }
    QFile report(QDir(directory).filePath("result.json"));if(!report.open(QIODevice::WriteOnly))return 4;
    report.write(QJsonDocument(QJsonObject{{"status",passed?"PASS":"FAIL_HOST"},{"plugin",plugin},
        {"baseline_instances",int(baseline)},{"cycles",cycles},
        {"limits","Five actual production Qt row/dialog open-close-destruction cycles in one process; host instance count only, not vendor internal allocations or live APO audio"}}).toJson());
    return passed?0:1;
}

inline int runPluginDialogAudit(QApplication& app,const QString& plugin,const QString& directory) {
    app.setProperty("engineeringAudit",true);QDir().mkpath(directory);
    auto library=VSTPluginLibrary::getInstance(plugin.toStdWString());
    if(library->initialize()<0)return 3;
    auto instance=std::make_unique<VSTPluginInstance>(library,1);
    if(!instance->initialize())return 4;
    QElapsedTimer total;total.start();qint64 closeMs=0,destroyMs=0;int ticks=0;
    const auto stage=[&](const char* name) {
        QFile file(QDir(directory).filePath("lifecycle-stages.log"));
        if(file.open(QIODevice::WriteOnly|QIODevice::Append)) {file.write(name);file.write("\n");file.flush();}
    };
    stage("instance_initialized");
    {
        VSTPluginFilterGUIDialog dialog(nullptr,instance.get(),false);
        QTimer ticker;QObject::connect(&ticker,&QTimer::timeout,&dialog,[&](){++ticks;});ticker.start(20);
        QTimer::singleShot(2000,&dialog,&QDialog::accept);
        dialog.exec();dialog.grab().save(QDir(directory).filePath("plugin-dialog.png"));
        stage("dialog_exec_returned");
        closeMs=total.elapsed();
    }
    stage("view_detached");
    const auto detached=total.elapsed();instance.reset();destroyMs=total.elapsed()-detached;
    stage("instance_destroyed");
    QFile report(QDir(directory).filePath("result.json"));if(!report.open(QIODevice::WriteOnly))return 5;
    const bool passed=total.elapsed()<5000 && ticks>=10;
    report.write(QJsonDocument(QJsonObject{{"status",passed?"PASS":"FAIL"},{"plugin",plugin},
        {"qt_ticks",ticks},{"dialog_elapsed_ms",closeMs},{"detach_ms",detached-closeMs},
        {"destroy_ms",destroyMs},{"total_ms",total.elapsed()},
        {"limits","Actual in-process Qt dialog and instance teardown, one cycle; no live APO DSP, GUI parameter PCM or repeated-cycle certification"}}).toJson());
    return passed?0:1;
}

inline int runBundledIrAudit(QApplication& app,const QString& directory) {
    app.setProperty("engineeringAudit",true);QDir().mkpath(directory);
    QWidget shell;auto* layout=new QVBoxLayout(&shell);
    const QString config=QDir(directory).filePath("selected-config.txt");
    auto* module=new ConvolutionFilterGUI(config,48000,QString(),QString());layout->addWidget(module);
    shell.resize(900,300);shell.show();
    QTimer::singleShot(100,&shell,[&shell,module,directory,config]() {
        QJsonArray selections;int failures=0;
        QToolButton* selector=nullptr;
        for(auto* button:module->findChildren<QToolButton*>())if(button->menu()) {selector=button;break;}
        QList<QAction*> actions;
        if(selector) {
            std::function<void(QMenu*)> collect=[&](QMenu* menu) {
                for(auto* action:menu->actions())if(action->menu())collect(action->menu());else actions.append(action);
            };collect(selector->menu());
        }
        for(auto* action:actions) {
            action->trigger();QString command,parameters;module->store(command,parameters);
            const auto absolute=QFileInfo(QFileInfo(config).absoluteDir(),parameters).absoluteFilePath();
            ConvolutionFilterGUI restored(config,48000,QString(),parameters);
            QString restoredCommand,restoredParameters;restored.store(restoredCommand,restoredParameters);
            const bool valid=command=="Convolution" && QFileInfo::exists(absolute) && restoredParameters==parameters;
            if(!valid)++failures;
            selections.append(QJsonObject{{"label",action->text()},{"path",absolute},{"parameters",parameters},{"restored",valid}});
        }
        shell.grab().save(QDir(directory).filePath("bundled-selector.png"));
        QFile file(QDir(directory).filePath("selector.json"));if(!file.open(QIODevice::WriteOnly))std::exit(3);
        file.write(QJsonDocument(QJsonObject{{"status",failures||actions.isEmpty()?"FAIL":"PASS"},
            {"count",actions.size()},{"failures",failures},{"selector_visible",selector && selector->isVisible()},
            {"manual_path_available",module->findChild<QLineEdit*>("pathLineEdit")!=nullptr},
            {"selections",selections},{"limits","Actual Qt selection and reconstruction; audio/analyzer verified separately; no installed APO proof"}}).toJson());file.close();
        std::exit(failures||actions.isEmpty()?1:0);
    });return app.exec();
}

inline std::wstring editorAuditCrashPath;
inline int runPathWidthAudit(QApplication& app,const QString& directory) {
    app.setProperty("engineeringAudit",true);QDir().mkpath(directory);
    QWidget shell;auto* layout=new QVBoxLayout(&shell);
    auto library=VSTPluginLibrary::getInstance(L"C:\\missing\\fixture.dll");
    auto* module=new VSTPluginFilterGUI(library,L"",{});layout->addWidget(module);
    shell.resize(1000,300);shell.show();
    QTimer::singleShot(100,&shell,[&shell,module,directory]() {
        auto* path=module->findChild<QLineEdit*>("pathLineEdit");
        if(!path)std::exit(3);
        path->setText("C:\\short.dll");module->layout()->activate();
        const int baseline=module->minimumSizeHint().width();
        const int widgets=module->findChildren<QWidget*>().size();int failures=0,maxWidth=baseline;
        for(unsigned cycle=0;cycle<400;++cycle) {
            const QString value=cycle%2 ? "C:\\short.dll" : "C:\\"+QString(1200,'x')+"\\plugin.dll";
            path->setText(value);module->layout()->invalidate();module->layout()->activate();
            const int width=module->minimumSizeHint().width();maxWidth=(std::max)(maxWidth,width);
            if(width!=baseline || module->findChildren<QWidget*>().size()!=widgets || path->toolTip()!=value)++failures;
        }
        shell.grab().save(QDir(directory).filePath("path-module.png"));
        QFile file(QDir(directory).filePath("result.json"));if(!file.open(QIODevice::WriteOnly))std::exit(3);
        file.write(QJsonDocument(QJsonObject{{"status",failures?"FAIL":"PASS"},{"cycles",400},
            {"baseline_minimum_width",baseline},{"maximum_minimum_width",maxWidth},{"failures",failures},
            {"widgets",widgets},{"limits","Real VST module layout with unavailable file; no plugin GUI, audio or monitor transition coverage"}}).toJson());file.close();
        std::exit(failures?1:0);
    });
    return app.exec();
}
inline LONG WINAPI editorAuditException(EXCEPTION_POINTERS* exception) {
    HANDLE file=CreateFileW(editorAuditCrashPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,0,nullptr);
    if(file!=INVALID_HANDLE_VALUE) {
        char line[2048];DWORD written=0;
        int length=snprintf(line,sizeof(line),"Exception %08lx address=%p\r\n",exception->ExceptionRecord->ExceptionCode,exception->ExceptionRecord->ExceptionAddress);
        WriteFile(file,line,length,&written,nullptr);
        HANDLE process=GetCurrentProcess();SymInitialize(process,nullptr,TRUE);
        CONTEXT context=*exception->ContextRecord;STACKFRAME64 frame{};
        frame.AddrPC={context.Rip,0,AddrModeFlat};frame.AddrStack={context.Rsp,0,AddrModeFlat};frame.AddrFrame={context.Rbp,0,AddrModeFlat};
        for(unsigned i=0;i<32;++i) {
            char symbolBuffer[sizeof(SYMBOL_INFO)+1024]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
            symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=1023;DWORD64 displacement=0;
            MEMORY_BASIC_INFORMATION memory{};VirtualQuery(reinterpret_cast<void*>(frame.AddrPC.Offset),&memory,sizeof(memory));
            char module[MAX_PATH]{};GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase),module,MAX_PATH);
            SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol);
            length=snprintf(line,sizeof(line),"%s + %llx %s + %llx\r\n",module,frame.AddrPC.Offset-reinterpret_cast<DWORD64>(memory.AllocationBase),symbol->Name,displacement);
            WriteFile(file,line,length,&written,nullptr);
            if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,GetCurrentThread(),&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
        }
        CloseHandle(file);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

inline int runConfigurationSaveAudit(QApplication& app,const QString& directory) {
    app.setProperty("engineeringAudit",true);QDir().mkpath(directory);
    const auto path=QDir(directory).filePath("config.txt");
    if(QFile::exists(path))return 2;
    {QFile file(path);if(!file.open(QIODevice::WriteOnly))return 3;file.write("Preamp: -3 dB\r\n");}
    auto* window=new MainWindow(QDir(directory));window->show();window->load(path);
    auto* preamp=window->findChild<QWidget*>("PreampFilterGUI");
    auto* gain=preamp?preamp->findChild<QDoubleSpinBox*>("doubleSpinBox"):nullptr;
    if(!gain)return 4;
    HANDLE reader=CreateFileW(path.toStdWString().c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    if(reader==INVALID_HANDLE_VALUE)return 5;
    auto ticks=std::make_shared<int>(0);auto* ticker=new QTimer(window);
    QObject::connect(ticker,&QTimer::timeout,window,[ticks](){++*ticks;});ticker->start(16);
    gain->setValue(-8);
    QTimer::singleShot(20,window,[gain](){gain->setValue(-7);});
    QTimer::singleShot(35,window,[gain](){gain->setValue(-6);});
    QTimer::singleShot(70,window,[reader](){CloseHandle(reader);});
    QTimer::singleShot(1000,window,[window,path,directory,ticks](){
        QFile file(path);const bool opened=file.open(QIODevice::ReadOnly);const auto text=opened?file.readAll():QByteArray();
        bool errorDialog=false;for(auto* dialog:window->findChildren<QMessageBox*>())errorDialog|=dialog->isVisible();
        const bool passed=opened && text.contains("Preamp: -6 dB") && *ticks>=20 && !errorDialog;
        QFile report(QDir(directory).filePath("result.json"));report.open(QIODevice::WriteOnly);
        report.write(QJsonDocument(QJsonObject{{"status",passed?"PASS":"FAIL"},{"qt_ticks",*ticks},
            {"saved_configuration",QString::fromUtf8(text)},{"error_dialog",errorDialog},
            {"limits","Real MainWindow, instant-mode spinbox changes and coalesced saving under an exclusive temporary reader; own file only, no installed APO configuration edits"}}).toJson());
        report.close();std::exit(passed?0:1);
    });
    return app.exec();
}

inline int runEditorUIAudit(QApplication& app,const QString& directory,const QString& config) {
    app.setProperty("engineeringAudit",true);
    if(qEnvironmentVariableIsSet("EQAPO_UI_RACK_ACTIONS"))app.setProperty("engineeringRackActions",qEnvironmentVariableIntValue("EQAPO_UI_RACK_ACTIONS")!=0);
    QDir().mkpath(directory);
    editorAuditCrashPath=QDir(directory).filePath("crash.txt").toStdWString();
    SetUnhandledExceptionFilter(editorAuditException);
    // Do not load or overwrite the user's saved window/file preferences.
    auto* window=new MainWindow(QDir(QFileInfo(config).absolutePath()));
    const auto available=QGuiApplication::primaryScreen()->availableGeometry();
    window->resize((std::min)(1200,available.width()-24),(std::min)(800,available.height()-48));
    window->move(available.topLeft()+QPoint(12,24));window->show();window->load(config);
    QTimer::singleShot(1000,window,[window,directory]() {
        QJsonArray screens,widgets;
        for(auto* screen:QGuiApplication::screens()) {
            const auto r=screen->availableGeometry();
            screens.append(QJsonObject{{"name",screen->name()},{"x",r.x()},{"y",r.y()},
                {"width",r.width()},{"height",r.height()},{"logical_dpi",screen->logicalDotsPerInch()},
                {"dpr",screen->devicePixelRatio()}});
        }
        int resetChecks=0,resetFailures=0;
        // Keep model reconstruction out of this isolated widget event check.
        std::vector<std::unique_ptr<QSignalBlocker>> blockers;
        for(auto* widget:window->findChildren<QWidget*>())blockers.emplace_back(std::make_unique<QSignalBlocker>(widget));
        for(auto* widget:window->findChildren<QWidget*>()) {
            const auto r=widget->geometry();
            widgets.append(QJsonObject{{"class",widget->metaObject()->className()},{"name",widget->objectName()},
                {"visible",widget->isVisible()},{"x",r.x()},{"y",r.y()},{"width",r.width()},{"height",r.height()},
                {"minimum_width",widget->minimumWidth()},{"minimum_height",widget->minimumHeight()}});
            const auto neutral=widget->property("defaultValue");
            const auto targetNeutral=widget->property("defaultTargetValue");
            if(!widget->isVisible())continue;
            QDoubleSpinBox* doubleTarget=qobject_cast<QDoubleSpinBox*>(widget);
            QSpinBox* intTarget=qobject_cast<QSpinBox*>(widget);
            QVariant expected=neutral;
            if(auto* slider=qobject_cast<QAbstractSlider*>(widget)) {
                if(qobject_cast<QScrollBar*>(slider))continue;
                if(targetNeutral.isValid()) {
                    auto* target=widget->property("resetTarget").value<QObject*>();
                    doubleTarget=qobject_cast<QDoubleSpinBox*>(target);
                    intTarget=qobject_cast<QSpinBox*>(target);expected=targetNeutral;
                } else if(neutral.isValid()) {
                    slider->setValue(neutral.toInt()==slider->maximum()?slider->minimum():slider->maximum());
                    QMouseEvent press(QEvent::MouseButtonPress,QPointF(4,4),widget->mapToGlobal(QPoint(4,4)),Qt::RightButton,Qt::RightButton,Qt::NoModifier);
                    QApplication::sendEvent(widget,&press);++resetChecks;
                    if(slider->value()!=neutral.toInt())++resetFailures;
                    continue;
                }
            }
            if(!expected.isValid() || (!doubleTarget && !intTarget))continue;
            if(doubleTarget)doubleTarget->setValue(expected.toDouble()==doubleTarget->maximum()?doubleTarget->minimum():doubleTarget->maximum());
            if(intTarget)intTarget->setValue(expected.toInt()==intTarget->maximum()?intTarget->minimum():intTarget->maximum());
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(4,4),widget->mapToGlobal(QPoint(4,4)),Qt::RightButton,Qt::RightButton,Qt::NoModifier);
            QApplication::sendEvent(widget,&press);++resetChecks;
            if(doubleTarget && std::abs(doubleTarget->value()-expected.toDouble())>1e-6)++resetFailures;
            if(intTarget && intTarget->value()!=expected.toInt())++resetFailures;
        }
        blockers.clear(); // Restore native scroll signals before capturing later pages.
        window->grab().save(QDir(directory).filePath("editor.png"));
        int areaIndex=0;
        for(auto* area:window->findChildren<QScrollArea*>()) {
            if(!area->isVisible() || !area->verticalScrollBar()->maximum())continue;
            auto* bar=area->verticalScrollBar();const int previous=bar->value();
            const int step=(std::max)(1,area->viewport()->height()-24);
            for(int page=0,offset=0;page<48;++page,offset+=step) {
                bar->setValue((std::min)(offset,bar->maximum()));
                window->grab().save(QDir(directory).filePath(QString("area-%1-page-%2.png").arg(areaIndex).arg(page)));
                if(offset>=bar->maximum())break;
            }
            bar->setValue(previous);++areaIndex;
        }
        QJsonArray documents;
        for(auto* table:window->findChildren<FilterTable*>()) {
            QJsonArray lines;for(const auto& line:table->getLines())lines.append(line);
            documents.append(QJsonObject{{"path",table->getConfigPath()},{"lines",lines},
                {"empty_placeholders",table->findChildren<QWidget*>("emptyDocumentPlaceholder").size()}});
        }
        const QJsonObject root{{"status",resetFailures?"FAIL":"PASS"},{"checks","window capture, widget geometry and declared right-click defaults"},
            {"documents",documents},
            {"dpr",window->devicePixelRatioF()},{"screens",screens},{"widgets",widgets},
            {"right_click_checks",resetChecks},{"right_click_failures",resetFailures},
            {"limits","QT_SCALE_FACTOR runs do not certify physical mixed-DPI monitor transitions or visually prove every module"}};
        QFile file(QDir(directory).filePath("geometry.json"));
        if(!file.open(QIODevice::WriteOnly))std::exit(3);
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));file.close();
        // A sacrificial audit process exits without saving personal preferences.
        std::exit(resetFailures?1:0);
    });
    return app.exec();
}
