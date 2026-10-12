#include "stdafx.h"
#include "filters/OutProcVSTPluginFilter.h"
#include "outproc/BrokerStreamScope.h"
#include <chrono>
#include <thread>
#include <fstream>
#include <cmath>
#include <avrt.h>
#include <mmsystem.h>

int wmain(int argc,wchar_t** argv){
    if(argc<4 || argc>8)return 2;
    bool dbGain=false,formats=false,fullGrid=false,overlap=false,sameStream=false;for(int i=4;i<argc;++i){const std::wstring option=argv[i];if(option==L"--db-band-gain")dbGain=true;else if(option==L"--formats")formats=true;else if(option==L"--full-grid")fullGrid=true;else if(option==L"--overlap")overlap=true;else if(option==L"--same-stream-overlap"){overlap=true;sameStream=true;}else return 2;}
    if(overlap && (formats || fullGrid || dbGain))return 2;
    const std::wstring plugin=argv[1];const auto parameter=unsigned(std::stoul(argv[3]));
    GUID guid{};CoCreateGuid(&guid);wchar_t text[64]{};StringFromGUID2(guid,text,64);const auto host=std::wstring(text).substr(1,36);
    timeBeginPeriod(1);DWORD task=0;HANDLE priority=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task);
    wchar_t pacing[8]{};const bool highResolution=GetEnvironmentVariableW(L"EQAPO_HIGH_RES_PRODUCER_AUDIT",pacing,8)>0 && pacing[0]==L'1';
    HANDLE timer=highResolution?CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS):nullptr;
    if(highResolution && !timer){if(priority)AvRevertMmThreadCharacteristics(priority);timeEndPeriod(1);return 8;}
    bool passed=true;unsigned stablePid=0;std::uint64_t previousGeneration=0;
    std::unique_ptr<OutProcVSTPluginFilter> retained;
    EqApoBroker::StreamScope firstScope,secondScope;
    std::ofstream report(argv[2]);report<<"{\"cases\":[";
    for(unsigned cycle=0;cycle<(fullGrid?21u:6u);++cycle){
        const double gain=dbGain?(cycle%2?.65:.5):(cycle%2?.75:.25);
        const double linearGain=dbGain?std::pow(10.,(gain-.5)*3.):gain;
        const unsigned rates[]={48000,96000,44100,48000,96000,44100},sizes[]={32,256,480,960,64,512};
        const unsigned gridRates[]={44100,48000,96000},gridSizes[]={32,64,128,256,480,512,960};
        const unsigned rate=fullGrid?gridRates[cycle/7]:(formats?rates[cycle]:48000),frames=fullGrid?gridSizes[cycle%7]:(formats?sizes[cycle]:480);
        std::unordered_map<std::wstring,float> parameters{{L"#"+std::to_wstring(parameter),float(gain)}};
        if(dbGain){parameters[L"#0"]=1.f;parameters[L"#1"]=1.f;} // Pro-Q Band1 Used/Enabled; #3 is its gain.
        const auto scopedId=overlap?(cycle && !sameStream?secondScope:firstScope).instanceFor(host):L"";
        auto filter=std::make_unique<OutProcVSTPluginFilter>(plugin,L"",parameters,host,false,0,L"",EqApoControl::currentUserSid(),scopedId);
        filter->initialize(float(rate),frames,{L"L",L"R"});
        std::vector<double>a(frames),b(frames),x(frames),y(frames);double* in[]={a.data(),b.data()};double* out[]={x.data(),y.data()};double energy=0;bool finite=true;
        const unsigned blocks=(rate*(cycle==0?3:2)/2+frames-1)/frames,measured=(rate*3/10+frames-1)/frames;
        std::vector<double>oldX(frames),oldY(frames);double* oldOutput[]={oldX.data(),oldY.data()};double oldEnergy=0;
        auto deadline=std::chrono::steady_clock::now();
        std::uint64_t steadyMissing=0,steadyDropped=0,steadyEpoch=0;double steadyLateUs=0;
        for(unsigned n=0;n<blocks;++n){
            if(n==blocks-measured){const auto d=filter->getDiagnostics();steadyMissing=d.missing;steadyDropped=d.dropped;steadyEpoch=d.available?d.epoch:0;}
            if(n>=blocks-measured)steadyLateUs=(std::max)(steadyLateUs,std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-deadline).count());
            for(unsigned f=0;f<frames;++f)a[f]=b[f]=.1*std::sin((std::uint64_t(n)*frames+f)*6.283185307179586*997/rate);
            filter->process(out,in,frames);
            if(retained){retained->process(oldOutput,in,frames);if(n>=blocks-measured)for(unsigned f=0;f<frames;++f)oldEnergy+=oldX[f]*oldX[f];}
            for(unsigned f=0;f<frames;++f){finite &= std::isfinite(x[f]) && std::isfinite(y[f]);if(n>=blocks-measured)energy+=x[f]*x[f];}
            deadline+=std::chrono::nanoseconds(std::uint64_t(frames)*1000000000/rate);
            if(timer){const auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-std::chrono::steady_clock::now()).count();
                if(remaining>0){LARGE_INTEGER due;due.QuadPart=-(std::max)(1LL,remaining/100);if(!SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE) || WaitForSingleObject(timer,INFINITE)!=WAIT_OBJECT_0){passed=false;break;}}}
            else std::this_thread::sleep_until(deadline);
        }
        const auto diagnostic=filter->getDiagnostics();if(overlap && cycle==1)stablePid=0;if(!stablePid)stablePid=diagnostic.hostPid;
        const bool steadyValid=diagnostic.available && steadyEpoch && diagnostic.epoch==steadyEpoch && diagnostic.missing>=steadyMissing && diagnostic.dropped>=steadyDropped;
        const auto missingDelta=steadyValid?diagnostic.missing-steadyMissing:0,droppedDelta=steadyValid?diagnostic.dropped-steadyDropped:0;
        const double rms=std::sqrt(energy/(std::uint64_t(measured)*frames)),ratio=rms/(linearGain*.1/std::sqrt(2.));
        const double retainedRatio=retained?std::sqrt(oldEnergy/(std::uint64_t(measured)*frames))/(.25*.1/std::sqrt(2.)):1;
        const bool ok=finite && (!retained || std::abs(retainedRatio-1)<.035) && diagnostic.hostAlive && stablePid && diagnostic.hostPid==stablePid
            && diagnostic.parameterGeneration && diagnostic.parameterGeneration!=previousGeneration && std::abs(ratio-1)<.035;
        passed &= ok;previousGeneration=diagnostic.parameterGeneration;
        if(cycle)report<<',';report<<"{\"cycle\":"<<cycle<<",\"rate\":"<<rate<<",\"block\":"<<frames<<",\"quantum\":"<<diagnostic.processingQuantum<<",\"pass\":"<<(ok?"true":"false")<<",\"host_pid\":"<<diagnostic.hostPid
            <<",\"generation\":"<<diagnostic.parameterGeneration<<",\"gain\":"<<gain<<",\"expected_linear_gain\":"<<linearGain<<",\"rms_ratio\":"<<ratio
            <<",\"missing\":"<<diagnostic.missing<<",\"dropped\":"<<diagnostic.dropped
            <<",\"steady_valid\":"<<(steadyValid?"true":"false")<<",\"steady_missing\":"<<missingDelta<<",\"steady_dropped\":"<<droppedDelta<<",\"steady_max_late_us\":"<<steadyLateUs<<",\"retained_ratio\":"<<retainedRatio<<'}';
        if(overlap && cycle==0)retained=std::move(filter);else filter.reset();
    }
    if(timer)CloseHandle(timer);if(priority)AvRevertMmThreadCharacteristics(priority);timeEndPeriod(1);
    report<<"],\"high_resolution_producer\":"<<(highResolution?"true":"false")<<",\"same_stream_overlap\":"<<(sameStream?"true":"false")<<",\"full_format_grid\":"<<(fullGrid?"true":"false")<<",\"status\":\""<<(passed?"PASS":"FAIL_HOST")<<"\",\"limits\":\"Actual same-channel graph replacements; no GUI HWND, float precision matrix or installed audiodg certification\"}";
    return passed?0:1;
}
