#pragma once
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QVBoxLayout>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include "Editor/guis/VSTPluginFilterGUI.h"
#include "Editor/guis/VSTPluginFilterGUIDialog.h"
#include "filters/VSTPluginFilter.h"
#include "filters/VSTPluginFilterFactory.h"
#include "Editor/AnalysisThread.h"

// Engineering path in the actual Editor executable: production row/dialog,
// serialized identity and production factory/filter. Does not install an APO
// or connect updateModel to a MainWindow/file watcher.
inline int runRowLiveAudit(QApplication& app,const QString& plugin,const QString& directory,
                          unsigned id,double before,double expectedRatio) {
    app.setProperty("engineeringAudit",true);app.setProperty("rowLiveAuditOnTop",true);QDir().mkpath(directory);
    const QDir output(directory);
    QWidget shell;
    auto library=VSTPluginLibrary::getInstance(plugin.toStdWString());
    if(library->initialize()<0)return 3;
    VSTPluginFilterGUI row(library,L"",{{L"#"+std::to_wstring(id),float(before)}});
    row.loadPreferences(QVariantMap{{"autoApplyDialog",true}});
    QString command,parameters;row.store(command,parameters);
    QFile initial(output.filePath("initial-config.txt"));
    if(initial.open(QIODevice::WriteOnly))initial.write((command+": "+parameters+"\n").toUtf8());initial.close();
    VSTPluginFilterFactory factory;auto wideCommand=command.toStdWString(),wideParameters=parameters.toStdWString();
    auto filters=factory.createFilter(L"",wideCommand,wideParameters);if(filters.size()!=1)return 4;
    auto* filter=static_cast<VSTPluginFilter*>(filters.front());
    auto release=[](VSTPluginFilter* value){value->~VSTPluginFilter();MemoryHelper::free(value);};
    std::unique_ptr<VSTPluginFilter,decltype(release)> audioFilter(filter,release);
    audioFilter->initialize(48000,480,{L"L",L"R"});
    std::atomic<bool> baselineDone{false},closeRequested{false},closed{false},finished{false};
    bool finite=true,closedBeforeMeasurement=false;double rms[3]{};int qtTicks=0,modelUpdates=0;
    auto analyzer=std::make_unique<AnalysisThread>();
    std::vector<double> analyzerGains;QJsonArray analyzerResults;bool analyzerFinite=true;
    const QString analyzerConfig=output.filePath("analyzer-live-config.txt");
    auto requestAnalysis=[&]() {
        QString analysisCommand,analysisParameters;row.store(analysisCommand,analysisParameters);
        QSaveFile file(analyzerConfig);
        if(!file.open(QIODevice::WriteOnly)) {analyzerFinite=false;return;}
        file.write((analysisCommand+": "+analysisParameters+"\n").toUtf8());
        if(!file.commit()){analyzerFinite=false;return;}
        analyzer->setParameters(nullptr,0,0,analyzerConfig,65536);
    };
    QObject::connect(analyzer.get(),&AnalysisThread::analysisFinished,&row,[&]() {
        analyzer->beginGetResult();const unsigned status=analyzer->getAnalysisStatus();
        double gain=std::numeric_limits<double>::quiet_NaN();
        const int frames=analyzer->getFreqDataLength(),rate=analyzer->getFreqDataSampleRate();
        if(!status && frames>0 && rate>0) {
            const int bin=int(std::round(997.*frames/rate));
            const auto* data=analyzer->getFreqData();
            gain=20*std::log10(std::hypot(data[bin][0],data[bin][1]));
        }
        analyzer->endGetResult();
        analyzerFinite &= !status && std::isfinite(gain);
        if(!status && std::isfinite(gain))analyzerGains.push_back(gain);
        analyzerResults.append(QJsonObject{{"analysis_status",int(status)},
            {"gain_997hz_db",std::isfinite(gain)?QJsonValue(gain):QJsonValue()}});
    });
    QObject::connect(&row,&IFilterGUI::updateModel,&row,[&](){++modelUpdates;requestAnalysis();});
    analyzer->start();requestAnalysis();
    auto* layout=new QVBoxLayout(&shell);layout->addWidget(&row);shell.setWindowTitle("EqAPO row live audit");shell.resize(850,260);shell.show();
    std::thread pcm([&]() {
        double a[480],b[480],x[480],y[480];double* input[]={a,b};double* result[]={x,y};
        std::uint64_t position=0;auto deadline=std::chrono::steady_clock::now();
        for(unsigned stage=0;stage<3;++stage) {
            if(stage==1)baselineDone.store(true,std::memory_order_release);
            if(stage==2)closeRequested.store(true,std::memory_order_release);
            const unsigned blocks=stage==1?400:200;
            for(unsigned block=0;block<blocks;++block) {
                if(stage==2 && block==blocks-50)closedBeforeMeasurement=closed.load(std::memory_order_acquire);
                for(unsigned f=0;f<480;++f)a[f]=b[f]=.1*std::sin((position+f)*6.283185307179586*997/48000.);
                audioFilter->process(result,input,480);
                for(unsigned f=0;f<480;++f) {
                    finite &= std::isfinite(x[f]) && std::isfinite(y[f]);
                    if(block>=blocks-50)rms[stage]+=x[f]*x[f];
                }
                position+=480;deadline+=std::chrono::milliseconds(10);std::this_thread::sleep_until(deadline);
            }
            rms[stage]=std::sqrt(rms[stage]/24000.);
        }
        finished.store(true,std::memory_order_release);
    });
    QTimer::singleShot(100,&row,[&]() {
        QMetaObject::invokeMethod(&row,"on_openPanelButton_clicked",Qt::DirectConnection);
        closed.store(true,std::memory_order_release);
    });
    bool panelSaved=false;
    QTimer ticker;
    QObject::connect(&ticker,&QTimer::timeout,&row,[&]() {
        ++qtTicks;
        VSTPluginFilterGUIDialog* dialog=nullptr;
        for(auto* top:QApplication::topLevelWidgets())if(auto* candidate=qobject_cast<VSTPluginFilterGUIDialog*>(top))if(candidate->isVisible())dialog=candidate;
        if(dialog && baselineDone.load(std::memory_order_acquire) && !analyzerGains.empty() && !panelSaved) {
            if(auto* frame=dialog->findChild<QWidget*>("frame")) {
                RECT bounds{};const auto window=reinterpret_cast<HWND>(frame->winId());GetWindowRect(window,&bounds);
                QFile panel(output.filePath("panel.json"));
                if(panel.open(QIODevice::WriteOnly))panel.write(QJsonDocument(QJsonObject{
                    {"pid",int(GetCurrentProcessId())},{"hwnd",QString::number(quintptr(window))},
                    {"bounds",QJsonArray{int(bounds.left),int(bounds.top),int(bounds.right),int(bounds.bottom)}}}).toJson());
                panel.close();panelSaved=true;
            }
        }
        if(dialog && closeRequested.load(std::memory_order_acquire))dialog->reject();
        if(finished.load(std::memory_order_acquire))app.quit();
    });ticker.start(20);
    QTimer::singleShot(20000,&row,[](){std::exit(9);});
    app.exec();pcm.join();ticker.stop();analyzer.reset();
    QString finalCommand,finalParameters;row.store(finalCommand,finalParameters);
    QFile finalConfig(output.filePath("edited-config.txt"));
    if(finalConfig.open(QIODevice::WriteOnly))finalConfig.write((finalCommand+": "+finalParameters+"\n").toUtf8());finalConfig.close();
    const auto sequence=audioFilter->getAppliedParameterSequence(),generation=audioFilter->getParameterGeneration(),drops=audioFilter->getParameterDrops();
    audioFilter.reset();
    auto restoredCommand=finalCommand.toStdWString(),restoredParameters=finalParameters.toStdWString();
    auto restoredFilters=factory.createFilter(L"",restoredCommand,restoredParameters);
    double restoredRms=0;bool restoredFinite=true;
    if(restoredFilters.size()==1) {
        std::unique_ptr<VSTPluginFilter,decltype(release)> restored(static_cast<VSTPluginFilter*>(restoredFilters.front()),release);
        restored->initialize(48000,480,{L"L",L"R"});
        double a[480],b[480],x[480],y[480];double* input[]={a,b};double* result[]={x,y};
        auto deadline=std::chrono::steady_clock::now();
        for(unsigned block=0;block<100;++block) {
            for(unsigned f=0;f<480;++f)a[f]=b[f]=.1*std::sin((block*480+f)*6.283185307179586*997/48000.);
            restored->process(result,input,480);
            for(unsigned f=0;f<480;++f) {
                restoredFinite &= std::isfinite(x[f]) && std::isfinite(y[f]);
                if(block>=50)restoredRms+=x[f]*x[f];
            }
            deadline+=std::chrono::milliseconds(10);std::this_thread::sleep_until(deadline);
        }
        restoredRms=std::sqrt(restoredRms/24000.);
    }
    const double ratio=rms[1]/rms[0],closedRatio=rms[2]/rms[1];
    const double restoredRatio=restoredRms/rms[1];
    const double analyzerDelta=analyzerGains.size()>=2 ? analyzerGains.back()-analyzerGains.front() : std::numeric_limits<double>::quiet_NaN();
    const bool analyzerPassed=analyzerFinite && analyzerGains.size()>=2 && std::isfinite(analyzerDelta) &&
        std::abs(analyzerDelta-20*std::log10(expectedRatio))<.35;
    const bool passed=analyzerPassed && finite && panelSaved && generation && sequence>0 && !drops && modelUpdates>0 && qtTicks>=20 && closedBeforeMeasurement &&
        std::isfinite(ratio) && std::abs(ratio-expectedRatio)<.03*expectedRatio && std::abs(closedRatio-1)<.03 &&
        restoredFinite && std::isfinite(restoredRatio) && std::abs(restoredRatio-1)<.03;
    QFile report(output.filePath("result.json"));if(!report.open(QIODevice::WriteOnly))return 5;
    report.write(QJsonDocument(QJsonObject{{"status",passed?"PASS":"PARAMETER_FAILURE"},{"mode","INPROC"},{"plugin",plugin},
        {"parameter",int(id)},{"baseline_rms",rms[0]},{"edited_rms",rms[1]},{"ratio",ratio},{"expected_ratio",expectedRatio},
        {"after_close_rms",rms[2]},{"after_close_ratio",closedRatio},{"gui_closed_before_measurement",closedBeforeMeasurement},
        {"restored_rms",restoredRms},{"restored_ratio",restoredRatio},
        {"analyzer_gui_update_verified",analyzerPassed},{"analyzer_results",analyzerResults},
        {"analyzer_delta_db",std::isfinite(analyzerDelta)?QJsonValue(analyzerDelta):QJsonValue()},
        {"parameter_generation",QString::number(generation)},{"parameter_sequence",int(sequence)},{"parameter_drops",int(drops)},
        {"qt_ticks",qtTicks},{"model_updates",modelUpdates},
        {"gui_mirror_processed_frames",QString::number(row.property("monitorProcessedFrames").toULongLong())},
        {"limits","Actual Editor row GUI edit, persistent PCM, saved-state restore, and same AnalysisThread clone update from row updateModel; engineering atomic save connects analyzer, not MainWindow/file watcher; no installed audiodg or meter animation certification"}}).toJson());
    // A stack QWidget must leave its owning layout before the shell is deleted.
    layout->removeWidget(&row);row.setParent(nullptr);
    return passed?0:1;
}
