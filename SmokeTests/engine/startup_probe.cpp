#include "stdafx.h"
#include "filters/OutProcVSTPluginFilter.h"
#include "helpers/VSTPluginLibrary.h"
#include "helpers/VSTPluginInstance.h"
#include <cmath>
#include <chrono>
#include <fstream>
#include <thread>

int wmain(int argc,wchar_t** argv){
    if(argc!=3 && argc!=4)return 2;
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const std::wstring path=argv[1];std::wstring state;
    std::unordered_map<std::wstring,float> parameters;
    if(argc==4){
        auto library=VSTPluginLibrary::getInstance(path);if(library->initialize()<0)return 3;
        VSTPluginInstance instance(library,2);if(!instance.initialize())return 4;
        instance.prepareForProcessing(48000,480);instance.readFromEffect(state,parameters);
    }
    OutProcVSTPluginFilter filter(path,state,parameters,L"",false,0);
    filter.initialize(48000,480,{L"L",L"R"});
    auto started=std::chrono::steady_clock::now(),deadline=started;
    double a[480],b[480],x[480],y[480];double* input[]={a,b};double* output[]={x,y};
    long long firstConsumed=-1,firstReady=-1;bool finite=true;std::uint64_t totalConsumed=0;
    std::ofstream report(argv[2]);report<<"{\"timeline\":[";bool comma=false;
    for(unsigned block=0;block<1200;++block){
        for(unsigned f=0;f<480;++f)a[f]=b[f]=.1*std::sin((block*480+f)*6.283185307179586*997/48000.);
        filter.process(output,input,480);
        for(unsigned f=0;f<480;++f)finite &= std::isfinite(x[f]) && std::isfinite(y[f]);
        const auto d=filter.getDiagnostics();const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        if(d.consumed && firstConsumed<0)firstConsumed=ms;
        if(d.ready && firstReady<0)firstReady=ms;totalConsumed=d.consumed;
        if(block%50==0){if(comma)report<<',';comma=true;report<<"{\"ms\":"<<ms<<",\"alive\":"<<d.hostAlive<<",\"ready\":"<<d.ready<<",\"consumed\":"<<d.consumed<<",\"error\":"<<d.error<<'}'<<std::flush;}
        if(firstReady>=0 && ms>firstReady+2000)break;
        deadline+=std::chrono::milliseconds(10);std::this_thread::sleep_until(deadline);
    }
    const bool passed=finite && firstReady>=0 && totalConsumed>100;
    report<<"],\"status\":\""<<(passed?"PASS":"STARTUP_NOT_READY")<<"\",\"first_consumed_ms\":"<<firstConsumed<<",\"first_ready_ms\":"<<firstReady<<",\"consumed\":"<<totalConsumed<<"}";
    return passed?0:1;
}
