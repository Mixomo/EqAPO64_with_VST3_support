#pragma once
#include "filters/OutProcVSTPluginFilter.h"
#include "filters/OutProcVSTPluginFilterFactory.h"
#include <chrono>
#include <thread>
#include <cmath>
#include <tlhelp32.h>
#include <avrt.h>
#include <mmsystem.h>
#pragma comment(lib,"avrt.lib")
#pragma comment(lib,"winmm.lib")

inline int runProtectedRowLiveAudit(QApplication& app,const QString& plugin,const QString& directory) {
    app.setProperty("engineeringAudit",true);
    const bool verifyGraphHandoff=qEnvironmentVariableIntValue("EQAPO_GRAPH_HANDOFF_AUDIT")==1;
    const bool changeFormat=qEnvironmentVariableIntValue("EQAPO_GRAPH_FORMAT_AUDIT")==1;
    // Only the owned fixture accepts deterministic native click injection.
    if(QFileInfo(plugin).fileName()!=QStringLiteral("EqApoFixture.vst3"))return 2;
    QDir().mkpath(directory);const QDir output(directory);
    auto library=VSTPluginLibrary::getInstance(plugin.toStdWString());if(library->initialize()<0)return 3;
    VSTPluginFilterGUI row(library,L"",{{L"#7",1.f}},QString(),0,library->getVST3ClassUID(0));
    QString command,parameters;row.store(command,parameters);
    OutProcVSTPluginFilterFactory factory;auto cmd=command.toStdWString(),args=parameters.toStdWString();
    auto filters=factory.createFilter(L"",cmd,args);if(filters.size()!=1)return 4;
    auto* filter=dynamic_cast<OutProcVSTPluginFilter*>(filters.front());if(!filter)return 5;
    auto release=[](OutProcVSTPluginFilter* p){p->~OutProcVSTPluginFilter();MemoryHelper::free(p);};
    std::unique_ptr<OutProcVSTPluginFilter,decltype(release)> audio(filter,release);
    filter->initialize(48000,480,{L"L",L"R"});
    std::atomic<unsigned> stage{0};std::atomic<bool> done{false};double rms[3]{};bool finite=true;
    unsigned updates=0,ticks=0;bool opened=false,clicked=false,closed=false,reopened=false,reopenedActual=false;
    DWORD hostPid=0;QElapsedTimer clock;clock.start();
    QObject::connect(&row,&IFilterGUI::updateModel,&row,[&](){++updates;});
    std::thread pcm([&](){
        double a[480],b[480],x[480],y[480];double* in[]={a,b};double* out[]={x,y};std::uint64_t position=0;
        auto deadline=std::chrono::steady_clock::now();
        for(unsigned part=0;part<3;++part){stage.store(part);
            const unsigned blocks=part==1?400:200;
            for(unsigned n=0;n<blocks;++n){
                for(unsigned f=0;f<480;++f)a[f]=b[f]=.1*std::sin((position+f)*6.283185307179586*997/48000);
                filter->process(out,in,480);for(unsigned f=0;f<480;++f){finite &= std::isfinite(x[f]);if(n>=blocks-50)rms[part]+=x[f]*x[f];}
                position+=480;deadline+=std::chrono::milliseconds(10);std::this_thread::sleep_until(deadline);
            }rms[part]=std::sqrt(rms[part]/24000);
        }done.store(true);
    });
    row.show();QTimer timer;
    QObject::connect(&timer,&QTimer::timeout,&row,[&](){
        ++ticks;
        if(stage.load()==1 && !opened){QMetaObject::invokeMethod(&row,"on_openPanelButton_clicked",Qt::DirectConnection);opened=true;}
        if(opened && !clicked){
            HWND top=FindWindowW(L"EqApoNativeDspViewAudit",nullptr);DWORD pid=0;if(top)GetWindowThreadProcessId(top,&pid);
            const auto diagnostic=filter->getDiagnostics();
            const bool ownHost=diagnostic.hostPid==pid;
            if(top && pid && ownHost && diagnostic.hostAlive){
                HWND meter=nullptr;EnumChildWindows(top,[](HWND w,LPARAM value)->BOOL {wchar_t name[128]{};GetClassNameW(w,name,128);if(std::wstring(name)==L"EqApoFixtureMeter"){*reinterpret_cast<HWND*>(value)=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&meter));
                if(meter){hostPid=pid;PostMessageW(meter,WM_LBUTTONUP,0,MAKELPARAM(100,100));clicked=true;}
            }
        }
        if(stage.load()==2 && !closed){row.prepareDelete();closed=true;}
        if(closed && clock.elapsed()>7200 && !reopened){QMetaObject::invokeMethod(&row,"on_openPanelButton_clicked",Qt::DirectConnection);reopened=true;}
        if(reopened){HWND top=FindWindowW(L"EqApoNativeDspViewAudit",nullptr);DWORD pid=0;if(top)GetWindowThreadProcessId(top,&pid);reopenedActual=top && pid==hostPid;}
        if(done.load())app.quit();
    });timer.start(20);QTimer::singleShot(15000,&row,[](){std::exit(9);});app.exec();pcm.join();timer.stop();
    row.store(command,parameters);if(!verifyGraphHandoff)row.prepareDelete();const auto diagnostic=filter->getDiagnostics();
    HWND preservedView=verifyGraphHandoff?FindWindowW(L"EqApoNativeDspViewAudit",nullptr):nullptr;
    const double ratio=rms[1]/rms[0],closeRatio=rms[2]/rms[1];
    const bool passed=finite && clicked && closed && reopenedActual && updates>0 && diagnostic.hostAlive && diagnostic.parameterSequence>0
        && std::abs(ratio-.25)<.0075 && std::abs(closeRatio-1)<.03 && parameters.contains("#7 0.25");
    audio.reset();
    auto restoredCommand=command.toStdWString(),restoredParameters=parameters.toStdWString();
    auto restoredFilters=factory.createFilter(L"",restoredCommand,restoredParameters);double restoredRms=0;bool preservedInstance=false;
    double restoredMaxLateUs=0;std::uint64_t restoredMissing=0,restoredDropped=0;
    double restoredSteadyMaxLateUs=0;std::uint64_t restoredSteadyMissing=0,restoredSteadyDropped=0;
    bool restoredSteadyValid=false;
    if(restoredFilters.size()==1){
        std::unique_ptr<OutProcVSTPluginFilter,decltype(release)> restored(static_cast<OutProcVSTPluginFilter*>(restoredFilters.front()),release);
        const unsigned restoredRate=changeFormat?96000:48000,restoredFrames=changeFormat?256:480;
        restored->initialize(float(restoredRate),restoredFrames,{L"L",L"R"});
        std::vector<double>a(restoredFrames),b(restoredFrames),x(restoredFrames),y(restoredFrames);double* input[]={a.data(),b.data()};double* result[]={x.data(),y.data()};
        const unsigned blocks=restoredRate*3/2/restoredFrames,measured=restoredRate/2/restoredFrames;
        std::atomic<bool> restoredDone{false};
        std::thread restoredProducer([&]{
        timeBeginPeriod(1);DWORD task=0;HANDLE priority=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task);
        // Engineering producer only: compare Windows high-resolution pacing
        // with std::sleep_until without changing the real APO callback.
        HANDLE pacingTimer=qEnvironmentVariableIntValue("EQAPO_HIGH_RES_PRODUCER_AUDIT")==1
            ? CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS):nullptr;
        auto deadline=std::chrono::steady_clock::now();
        std::uint64_t measurementMissing=0,measurementDropped=0,measurementEpoch=0;
        for(unsigned n=0;n<blocks;++n){
            const double lateness=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-deadline).count();
            restoredMaxLateUs=(std::max)(restoredMaxLateUs,lateness);
            if(n==blocks-measured){const auto d=restored->getDiagnostics();measurementMissing=d.missing;measurementDropped=d.dropped;measurementEpoch=d.available?d.epoch:0;}
            if(n>=blocks-measured)restoredSteadyMaxLateUs=(std::max)(restoredSteadyMaxLateUs,lateness);
            for(unsigned f=0;f<restoredFrames;++f)a[f]=b[f]=.1*std::sin((std::uint64_t(n)*restoredFrames+f)*6.283185307179586*997/restoredRate);
            restored->process(result,input,restoredFrames);if(n>=blocks-measured)for(unsigned f=0;f<restoredFrames;++f)restoredRms+=x[f]*x[f];deadline+=std::chrono::nanoseconds(std::uint64_t(restoredFrames)*1000000000/restoredRate);
            if(pacingTimer){const auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-std::chrono::steady_clock::now()).count();
                if(remaining>0){LARGE_INTEGER due;due.QuadPart=-(std::max)(1LL,remaining/100);if(SetWaitableTimer(pacingTimer,&due,0,nullptr,nullptr,FALSE))WaitForSingleObject(pacingTimer,INFINITE);}}
            else std::this_thread::sleep_until(deadline);}
        const auto final=restored->getDiagnostics();restoredSteadyValid=final.available && measurementEpoch && final.epoch==measurementEpoch && final.missing>=measurementMissing && final.dropped>=measurementDropped;
        if(restoredSteadyValid){restoredSteadyMissing=final.missing-measurementMissing;restoredSteadyDropped=final.dropped-measurementDropped;}
        if(pacingTimer)CloseHandle(pacingTimer);
        if(priority)AvRevertMmThreadCharacteristics(priority);timeEndPeriod(1);restoredDone.store(true,std::memory_order_release);
        });
        while(!restoredDone.load(std::memory_order_acquire)){QCoreApplication::processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
        restoredProducer.join();
        restoredRms=std::sqrt(restoredRms/(std::uint64_t(measured)*restoredFrames));
        const auto restoredDiagnostic=restored->getDiagnostics();DWORD viewPid=0;
        restoredMissing=restoredDiagnostic.missing;restoredDropped=restoredDiagnostic.dropped;
        if(preservedView && IsWindow(preservedView))GetWindowThreadProcessId(preservedView,&viewPid);
        preservedInstance=preservedView && IsWindow(preservedView) && viewPid==hostPid && restoredDiagnostic.hostAlive && restoredDiagnostic.hostPid==hostPid;
    }
    row.prepareDelete();
    const bool restorePassed=std::isfinite(restoredRms) && std::abs(restoredRms/rms[1]-1)<.03;
    QFile file(output.filePath("result.json"));if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{
        {"status",passed && restorePassed && (!verifyGraphHandoff || preservedInstance)?"PASS":"FAIL_HOST"},{"native_host_pid",int(hostPid)},{"qt_ticks",int(ticks)},
        {"graph_handoff_trial",verifyGraphHandoff},{"graph_handoff_preserved_instance_and_view",preservedInstance},
        {"graph_format_change_trial",changeFormat},
        {"restored_producer_max_late_us",restoredMaxLateUs},{"restored_missing_blocks",double(restoredMissing)},{"restored_dropped_blocks",double(restoredDropped)},
        {"restored_steady_max_late_us",restoredSteadyMaxLateUs},{"restored_steady_missing_blocks",double(restoredSteadyMissing)},{"restored_steady_dropped_blocks",double(restoredSteadyDropped)},
        {"restored_steady_window_valid",restoredSteadyValid},
        {"model_updates",int(updates)},{"native_click_sent",clicked},{"closed",closed},{"reopened",reopenedActual},
        {"baseline_rms",rms[0]},{"edited_rms",rms[1]},{"after_close_rms",rms[2]},{"gain_ratio",ratio},{"after_close_ratio",closeRatio},
        {"restored_rms",restoredRms},{"restore_ratio",restoredRms/rms[1]},{"parameter_sequence",int(diagnostic.parameterSequence)},{"saved_parameters",parameters},
        {"limits","Actual Editor protected row and its real interactive DSP (private or opt-in broker route); no installed audiodg or MainWindow reload certification"}}).toJson());
    return passed && restorePassed && (!verifyGraphHandoff || preservedInstance)?0:1;
}
