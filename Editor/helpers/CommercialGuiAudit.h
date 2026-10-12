#pragma once
#include "FilterEngine.h"
#include <fstream>
#include <sstream>
#include <mutex>
#include <QCryptographicHash>
#include <QUuid>

// Own config, actual MainWindow/row/dialog and FilterEngine. The private loader
// follows the production rule of waiting for an already-published graph before
// reading the newest config. Filesystem work never runs in the PCM producer.
inline int runCommercialGuiAudit(QApplication& app,const QString& plugin,const QString& directory,
                                 bool protectedMode,unsigned seconds) {
    if(seconds<12 || seconds>40)return 2;
    app.setProperty("engineeringAudit",true);app.setProperty("rowLiveAuditOnTop",true);
    QDir().mkpath(directory);const QDir output(directory);const auto config=output.filePath("config.txt");
    if(QFile::exists(config))return 3;
    const auto session=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto sid=QString::fromStdWString(EqApoControl::currentUserSid());
    const auto initial=(protectedMode?QString("OutProcVSTPlugin: "):QString("VSTPlugin: "))+
        "Library \""+QDir::toNativeSeparators(plugin)+"\" HostId "+session+" GuiUserSID "+sid+
        " "+qEnvironmentVariable("EQAPO_COMMERCIAL_INITIAL_PARAMS")+"\r\n";
    {QFile f(config);if(!f.open(QIODevice::WriteOnly))return 4;f.write(initial.toUtf8());}
    auto window=std::make_unique<MainWindow>(QDir(directory));window->resize(1100,650);window->show();window->load(config);
    auto* row=window->findChild<VSTPluginFilterGUI*>();if(!row)return 5;
    FilterEngine engine;engine.initialize(48000,2,2,2,3,480,config.toStdWString(),true);
    std::atomic<bool> stop{false};std::atomic<unsigned> reloads{0};std::mutex timelineMutex;
    QJsonArray timeline;bool finite=true;double maxProcessUs=0;unsigned zeros=0;
    const auto configPath=config.toStdWString();
    std::thread loader([&](){
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);std::string previous=initial.toStdString();
        while(!stop.load(std::memory_order_acquire)) {
            if(!engine.hasPendingConfiguration()) {
                std::ifstream file(configPath,std::ios::binary);std::ostringstream content;content<<file.rdbuf();
                const auto bytes=content.str();
                if(file && !bytes.empty() && bytes!=previous){engine.loadConfig(configPath);previous=bytes;reloads.fetch_add(1);}
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }CoUninitialize();
    });
    std::thread pcm([&](){
        double a[480],b[480],x[480],y[480];double* in[]={a,b};double* out[]={x,y};
        double sum=0,inputSum=0,peak=0;auto deadline=std::chrono::steady_clock::now();
        for(unsigned n=0;n<seconds*100 && !stop.load();++n){
            // Reproducible broad input with five frequencies, identical stereo.
            for(unsigned f=0;f<480;++f){const auto t=double(n*480+f)/48000.;
                a[f]=b[f]=(.55+.45*std::sin(t*6.283185307179586*.41))*(.025*std::sin(t*6.283185307179586*83)+.04*std::sin(t*6.283185307179586*997)+
                    .018*std::sin(t*6.283185307179586*6003)+.012*std::sin(t*6.283185307179586*311)+.008*std::sin(t*6.283185307179586*12001));}
            const auto began=std::chrono::steady_clock::now();engine.process(out,in,480);
            maxProcessUs=(std::max)(maxProcessUs,std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-began).count());
            bool silent=true;for(unsigned f=0;f<480;++f){finite &= std::isfinite(x[f]) && std::isfinite(y[f]);sum+=x[f]*x[f];inputSum+=a[f]*a[f];peak=(std::max)(peak,std::abs(x[f]));silent &= x[f]==0 && y[f]==0;}
            zeros+=silent;
            if(n%50==49){std::lock_guard<std::mutex> lock(timelineMutex);timeline.append(QJsonObject{{"seconds",(n+1)/100.},{"rms",std::sqrt(sum/24000.)},{"input_rms",std::sqrt(inputSum/24000.)},{"normalized_gain",std::sqrt(sum/inputSum)},{"peak",peak}});sum=inputSum=peak=0;}
            deadline+=std::chrono::milliseconds(10);std::this_thread::sleep_until(deadline);
        }
    });
    QElapsedTimer clock;clock.start();unsigned ticks=0;bool panelSaved=false,closed=false,nativeProcessorPanel=false;int updates=0;HWND lastPanel=nullptr;double firstPanelSeconds=-1;
    QObject::connect(row,&IFilterGUI::updateModel,window.get(),[&](){++updates;});
    QTimer ticker;
    QObject::connect(&ticker,&QTimer::timeout,window.get(),[&](){
        ++ticks;const auto now=clock.elapsed();HWND native=nullptr;DWORD pid=0;
        VSTPluginFilterGUIDialog* dialog=nullptr;
        for(auto* top:QApplication::topLevelWidgets())if(auto* d=qobject_cast<VSTPluginFilterGUIDialog*>(top))if(d->isVisible())dialog=d;
        if(dialog){if(auto* frame=dialog->findChild<QWidget*>("frame"))native=reinterpret_cast<HWND>(frame->winId());pid=GetCurrentProcessId();}
        else if(protectedMode){
            const auto owned=row->getOwnedGuiProcessId();auto library=VSTPluginLibrary::getInstance(plugin.toStdWString());
            QString cmd,args;row->store(cmd,args);auto currentSession=session;
            auto currentUID=library->getVST3ClassUID(0);
            const auto parts=StringHelper::splitQuoted(args.toStdWString(),' ');for(unsigned i=0;i+1<parts.size();i+=2){
                if(parts[i]==L"HostId")currentSession=QString::fromStdWString(parts[i+1]);
                if(parts[i]==L"ClassUID")currentUID=parts[i+1];
            }
            const auto status=EqApoControl::nativeViewControl(currentSession.toStdWString(),EqApoControl::controlIdentity(plugin.toStdWString(),currentUID,0));
            struct Target{DWORD pid;HWND hwnd;};Target target{owned?owned:status.pid,nullptr};
            if(target.pid)EnumWindows([](HWND w,LPARAM data)->BOOL{auto* t=reinterpret_cast<Target*>(data);DWORD p=0;GetWindowThreadProcessId(w,&p);if(p==t->pid && IsWindowVisible(w)){t->hwnd=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&target));
            native=target.hwnd;pid=target.pid;
            if(native && !owned)nativeProcessorPanel=true;
        }
        if(native && now>=3000 && native!=lastPanel){RECT rect{};GetWindowRect(native,&rect);
            QSaveFile panel(output.filePath("panel.json"));if(panel.open(QIODevice::WriteOnly)){panel.write(QJsonDocument(QJsonObject{{"pid",int(pid)},{"hwnd",QString::number(quintptr(native))},{"bounds",QJsonArray{int(rect.left),int(rect.top),int(rect.right),int(rect.bottom)}}}).toJson());panel.commit();}
            if(!panelSaved){QFile before(config);if(before.open(QIODevice::ReadOnly)){QFile baseline(output.filePath("panel-ready-config.txt"));if(baseline.open(QIODevice::WriteOnly))baseline.write(before.readAll());}}
            if(!panelSaved)firstPanelSeconds=now/1000.;panelSaved=true;lastPanel=native;
        }
        if(now>=int(seconds-3)*1000 && !closed){if(dialog)dialog->reject();row->prepareDelete();closed=true;}
        if(now>=int(seconds)*1000)app.quit();
    });ticker.start(20);
    QTimer::singleShot(1200,window.get(),[&](){QMetaObject::invokeMethod(row,"on_openPanelButton_clicked",Qt::DirectConnection);});
    QTimer::singleShot(50000,window.get(),[](){std::exit(9);});app.exec();ticker.stop();
    stop.store(true,std::memory_order_release);pcm.join();loader.join();
    const auto mirrorFrames=row->property("monitorProcessedFrames").toULongLong();row->prepareDelete();
    QFile final(config);QByteArray bytes;if(final.open(QIODevice::ReadOnly))bytes=final.readAll();
    QFile edited(output.filePath("edited-config.txt"));if(edited.open(QIODevice::WriteOnly))edited.write(bytes);
    QFile report(output.filePath("result.json"));if(!report.open(QIODevice::WriteOnly))return 6;
    report.write(QJsonDocument(QJsonObject{{"status",finite && panelSaved && closed?"OBSERVED":"HARNESS_FAILURE"},
        {"plugin",plugin},{"mode",protectedMode?"OUTPROC":"INPROC"},{"seconds",int(seconds)},{"qt_ticks",int(ticks)},
        {"model_updates",updates},{"graph_reloads",int(reloads.load())},{"gui_mirror_processed_frames",QString::number(mirrorFrames)},
        {"finite",finite},{"zero_blocks",int(zeros)},{"max_process_us",maxProcessUs},{"timeline",timeline},
        {"panel_observed",panelSaved},{"native_processor_panel",nativeProcessorPanel},{"first_panel_seconds",firstPanelSeconds},{"gui_closed",closed},{"final_config_sha256",QString(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())},
        {"limits","Actual MainWindow saving and FilterEngine private PCM with control-thread config reloads; observations require a separate plugin-specific GUI/audio oracle; no installed audiodg certification"}}).toJson());
    return finite && panelSaved && closed?0:1;
}
