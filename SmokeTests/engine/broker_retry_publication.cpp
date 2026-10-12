#include "stdafx.h"
#include "filters/OutProcVSTPluginFilter.h"
#include <chrono>
#include <thread>
#include <fstream>
#include <cmath>

int wmain(int argc,wchar_t** argv){
    if(argc!=3 && argc!=4)return 2;
    const bool initialFailure=argc==4 && std::wstring(argv[3])==L"--initial-failure";
    if(argc==4 && !initialFailure)return 2;
    SetEnvironmentVariableW(L"EQAPO_USER_BROKER",L"1");
    GUID unique{};if(FAILED(CoCreateGuid(&unique)))return 3;
    // A random, nonexistent target principal isolates this catalog from all
    // actual user brokers and installed endpoints; creator retains access.
    const std::wstring sid=L"S-1-5-21-"+std::to_wstring(unique.Data1)+L"-"+std::to_wstring(unsigned(unique.Data2))+L"-"+std::to_wstring(unsigned(unique.Data3))+L"-424242";
    const std::wstring id=L"retry-publication-"+std::to_wstring(unique.Data1);
    OutProcVSTConfig config;config.libraryPath=argv[1];
    EqApoBroker::Owner filler;if(!filler.create(id,sid,config,48000,2,480))return 4;
    EqApoBroker::Catalog catalog;if(!catalog.open(filler.space,sid,true))return 5;
    for(unsigned i=0;i<(initialFailure?64u:63u);++i)if(!catalog.publish(filler))return 6;
    OutProcVSTPluginFilter filter(argv[1],L"",{{L"#7",.25f}},id,false,0,L"",sid);
    filter.initialize(48000,480,{L"L",L"R"});
    const bool initialAvailable=filter.getDiagnostics().available;
    double a[480],b[480],x[480],y[480];double* in[]={a,b};double* out[]={x,y};
    for(unsigned i=0;i<480;++i)a[i]=b[i]=.1;
    bool finite=true;auto deadline=std::chrono::steady_clock::now();
    for(unsigned n=0;n<(initialFailure?100u:700u);++n){filter.process(out,in,480);for(unsigned i=0;i<480;++i)finite&=std::isfinite(x[i]);deadline+=std::chrono::milliseconds(10);std::this_thread::sleep_until(deadline);}
    const auto diagnostic=filter.getDiagnostics();const bool ready=filter.isReadyForTransition(),available=diagnostic.available;
    const bool passed=initialAvailable!=initialFailure && !available && diagnostic.launchFailed && ready && finite && std::abs(x[479]-.1)<1e-9;
    std::ofstream report(argv[2]);report<<"{\"initial_failure_trial\":"<<(initialFailure?"true":"false")<<",\"initial_available\":"<<(initialAvailable?"true":"false")<<",\"final_available\":"<<(available?"true":"false")<<",\"launch_failed\":"<<(diagnostic.launchFailed?"true":"false")<<",\"transition_ready\":"<<(ready?"true":"false")<<",\"dry_sample\":"<<x[479]<<",\"status\":\""<<(passed?"PASS":"FAIL_HOST")<<"\"}";
    return passed?0:1;
}
