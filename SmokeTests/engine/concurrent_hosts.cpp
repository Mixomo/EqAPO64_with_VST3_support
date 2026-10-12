#include "stdafx.h"
#include "filters/OutProcVSTPluginFilter.h"
#include <chrono>
#include <fstream>
#include <cmath>
#include <avrt.h>
#include <mmsystem.h>

int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;
    const std::wstring plugin=argv[1];
    std::unique_ptr<OutProcVSTPluginFilter> first,second;
    std::atomic<bool> go{false};
    std::thread createA([&](){while(!go.load())std::this_thread::yield();first=std::make_unique<OutProcVSTPluginFilter>(plugin,L"",std::unordered_map<std::wstring,float>{{L"#7",.25f}},L"",false);first->initialize(48000,480,{L"L",L"R"});});
    std::thread createB([&](){while(!go.load())std::this_thread::yield();second=std::make_unique<OutProcVSTPluginFilter>(plugin,L"",std::unordered_map<std::wstring,float>{{L"#7",.75f}},L"",false);second->initialize(48000,480,{L"L",L"R"});});
    go.store(true);createA.join();createB.join();
    timeBeginPeriod(1);std::atomic<unsigned> peerBlocks{0};
    double firstRms=0,secondBefore=0,secondAfter=0;bool finiteA=true,finiteB=true;
    auto render=[](OutProcVSTPluginFilter* filter,unsigned blocks,double* before,double* after,bool& finite,std::atomic<unsigned>* counter){
        DWORD index=0;HANDLE priority=AvSetMmThreadCharacteristicsW(L"Pro Audio",&index);
        double a[480],b[480],x[480],y[480];double* input[]={a,b};double* output[]={x,y};
        auto deadline=std::chrono::steady_clock::now();
        for(unsigned n=0;n<blocks;++n){
            for(unsigned f=0;f<480;++f)a[f]=b[f]=.1*std::sin((n*480+f)*6.283185307179586*997/48000);
            filter->process(output,input,480);
            for(unsigned f=0;f<480;++f){finite &= std::isfinite(x[f]) && std::isfinite(y[f]);
                if(n>=250 && n<300)*before+=x[f]*x[f];
                if(after && n>=blocks-50)*after+=x[f]*x[f];}
            if(counter)counter->store(n+1);deadline+=std::chrono::milliseconds(10);std::this_thread::sleep_until(deadline);
        }
        *before=std::sqrt(*before/24000);if(after)*after=std::sqrt(*after/24000);
        if(priority)AvRevertMmThreadCharacteristics(priority);
    };
    std::thread audioA([&](){render(first.get(),300,&firstRms,nullptr,finiteA,nullptr);});
    std::thread audioB([&](){render(second.get(),600,&secondBefore,&secondAfter,finiteB,&peerBlocks);});
    audioA.join();const auto firstDiagnostic=first->getDiagnostics();
    const unsigned blocksBeforeClose=peerBlocks.load();first.reset();const unsigned blocksAfterClose=peerBlocks.load();
    audioB.join();timeEndPeriod(1);const auto last=second->getDiagnostics();
    const double expected=.1/std::sqrt(2.);
    const bool passed=finiteA && finiteB && firstDiagnostic.hostAlive && last.hostAlive && firstDiagnostic.consumed>=200 && last.consumed>=500
        && std::abs(firstRms/(.25*expected)-1)<.03 && std::abs(secondBefore/(.75*expected)-1)<.03 && std::abs(secondAfter/secondBefore-1)<.03;
    std::ofstream report(argv[2]);report<<"{\"status\":\""<<(passed?"PASS":"FAIL_HOST")<<"\",\"first_rms\":"<<firstRms
        <<",\"peer_before_rms\":"<<secondBefore<<",\"peer_after_rms\":"<<secondAfter<<",\"peer_close_ratio\":"<<secondAfter/secondBefore
        <<",\"peer_blocks_before_close\":"<<blocksBeforeClose<<",\"peer_blocks_after_close\":"<<blocksAfterClose
        <<",\"peer_consumed\":"<<last.consumed<<",\"limits\":\"Two private DSP graphs and concurrent creation; no installed audiodg, GUI, arbitrary plugin or complete kernel-object lifetime certification\"}";
    return passed?0:1;
}
