#include "stdafx.h"
#include "FilterEngine.h"
#include "outproc/OutProcControlEndpoint.h"
#include <fstream>
#include <chrono>
#include <thread>
#include <cmath>
int wmain(int argc,wchar_t** argv){
    if(argc!=4)return 2;
    const std::wstring plugin=argv[1],path=argv[2],session=L"pending-control-"+std::to_wstring(GetCurrentProcessId());
    const auto sid=EqApoControl::currentUserSid();const auto identity=EqApoControl::controlIdentity(plugin,L"",0);
    auto write=[&](double gain){std::wofstream f(path);f<<L"OutProcVSTPlugin: Library \""<<plugin<<L"\" HostId "<<session<<L" GuiUserSID "<<sid<<L" #7 "<<gain<<L"\n";return f.good();};
    if(!write(.25))return 3;
    FilterEngine engine;engine.initialize(48000,2,2,2,3,480,path,true);
    double input[480],left[480],right[480];for(auto& sample:input)sample=.1;
    double* in[]={input,input};double* out[]={left,right};
    double baseline=0,early=0,settled=0;bool finite=true,accepted=false;
    unsigned earlyCount=0,settledCount=0;double firstResponseMs=-1;
    for(unsigned phase=0;phase<2;++phase){
        if(phase){SetEnvironmentVariableW(L"EQAPO_FIXTURE_INITIALIZE_DELAY_MS",L"3000");if(!write(.75))return 3;engine.loadConfig(path);}
        auto deadline=std::chrono::steady_clock::now();
        for(unsigned n=0;n<(phase?500u:150u);++n){
            if(phase && n==20)accepted=EqApoControl::publishControl(session,identity,7,.5);
            engine.process(out,in,480);double mean=0;
            for(auto sample:left){finite&=std::isfinite(sample);mean+=sample/480.;}
            if(!phase && n>=120)baseline+=mean/30.;
            if(phase && n>=20 && firstResponseMs<0 && std::abs(mean-.05)<.001)firstResponseMs=(n-20)*10.;
            if(phase && n>=40 && n<100){early+=mean;++earlyCount;}
            if(phase && n>=450){settled+=mean;++settledCount;}
            deadline+=std::chrono::milliseconds(10);std::this_thread::sleep_until(deadline);
        }
    }
    early/=earlyCount;settled/=settledCount;
    const bool passed=finite && accepted && std::abs(baseline-.025)<.001 && std::abs(early-.05)<.001 && std::abs(settled-.05)<.001 && firstResponseMs>=0 && firstResponseMs<=150;
    std::ofstream report(argv[3]);report<<"{\"status\":\""<<(passed?"PASS":"FAIL")<<"\",\"baseline\":"<<baseline<<",\"during_loading\":"<<early<<",\"after_replacement\":"<<settled<<",\"first_edit_response_ms\":"<<firstResponseMs<<",\"replacement_sdk_delay_ms\":3000,\"accepted\":"<<(accepted?"true":"false")<<",\"finite\":"<<(finite?"true":"false")<<",\"limits\":\"Actual private FilterEngine PCM; fixture SDK delay; no installed audiodg certification\"}";
    return passed?0:1;
}
