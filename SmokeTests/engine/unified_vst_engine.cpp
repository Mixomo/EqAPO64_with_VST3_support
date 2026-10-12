#include "stdafx.h"
#include "FilterEngine.h"
#include "outproc/OutProcControlEndpoint.h"
#include <fstream>
#include <thread>
#include <chrono>
#include <iostream>
#include <cmath>

static HWND findView(unsigned pid) {
    struct Search {unsigned pid;HWND window=nullptr;} search{pid};
    EnumWindows([](HWND window,LPARAM raw)->BOOL {
        auto& search=*reinterpret_cast<Search*>(raw);DWORD owner=0;GetWindowThreadProcessId(window,&owner);
        wchar_t text[128]{};GetWindowTextW(window,text,128);
        if(owner==search.pid && IsWindowVisible(window) && std::wstring(text)==L"Equalizer APO - VST")search.window=window;
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));return search.window;
}

// Production configuration factories, not directly constructed legacy filters.
// Tests both command aliases and both SDK formats using deterministic fixtures.
int wmain(int argc,wchar_t** argv) {
    if(argc!=6)return 2;
    const std::wstring plugin=argv[1],alias=argv[3],session=argv[4],parameter=argv[5];
    if(alias!=L"VSTPlugin" && alias!=L"OutProcVSTPlugin")return 2;
    const auto identity=EqApoControl::controlIdentity(plugin,L"",0);
    auto write=[&](unsigned phase) {
        std::wofstream f(argv[2]);if(phase==1)f<<L"# ";
        f<<alias<<L": Library \""<<plugin<<L"\" HostId "<<session<<L" GuiUserSID "<<EqApoControl::currentUserSid()
            <<L" "<<parameter<<L" "<<(phase==2?.75:phase==3?.5:.25)<<L"\n";return f.good();
    };
    if(!write(0))return 3;
    FilterEngine engine;engine.initialize(44100,2,2,2,3,441,argv[2],true);
    double input[441],left[441],right[441];double* in[]={input,input};double* out[]={left,right};
    for(auto& sample:input)sample=.1;
    unsigned pids[4]{};HWND views[4]{};long long readiness[4]{-1,-1,-1,-1};double means[4]{};unsigned counts[4]{};bool finite=true;
    for(unsigned phase=0;phase<4;++phase) {
        if(phase){if(!write(phase))return 3;engine.loadConfig(argv[2]);}
        const auto began=std::chrono::steady_clock::now();auto deadline=began;
        for(unsigned block=0;block<1400;++block) {
            engine.process(out,in,441);
            for(auto v:left)finite &= std::isfinite(v);
            const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count();
            if(block%10==0) {
                const auto view=EqApoControl::nativeViewControl(session,identity);
                const bool ready=!engine.hasPendingConfiguration() && (phase==1 || (view.available && view.status>=1 && view.status<=3));
                if(ready && readiness[phase]<0){readiness[phase]=ms;pids[phase]=view.pid;}
                if(phase==0 && ready)EqApoControl::nativeViewControl(session,identity,1);
            }
            if(readiness[phase]>=0 && ms>readiness[phase]+600){for(auto v:left){means[phase]+=v;++counts[phase];}}
            if(readiness[phase]>=0 && ms>readiness[phase]+(phase==1?7000:1600))break;
            deadline+=std::chrono::milliseconds(10);std::this_thread::sleep_until(deadline);
        }
        views[phase]=findView(pids[phase]);if(counts[phase])means[phase]/=counts[phase];
    }
    const bool retained=pids[0] && pids[0]==pids[2] && views[0] && views[0]==views[2];
    const bool independent=pids[3] && pids[3]!=pids[2];
    const bool state=std::abs(means[0]-.025)<1e-5 && std::abs(means[1]-.1)<1e-5 && std::abs(means[2]-.075)<1e-5 && std::abs(means[3]-.05)<1e-5;
    const bool passed=finite && retained && independent && state && readiness[2]>=0 && readiness[2]<1000;
    EqApoControl::nativeViewControl(session,identity,3);
    std::cout<<"{\"passed\":"<<(passed?"true":"false")<<",\"same_worker_and_gui_after_bypass\":"<<(retained?"true":"false")
        <<",\"adjacent_active_generations_independent\":"<<(independent?"true":"false")<<",\"updated_state_applied\":"<<(state?"true":"false")
        <<",\"reactivation_ms\":"<<readiness[2]<<",\"output_means\":["<<means[0]<<","<<means[1]<<","<<means[2]<<","<<means[3]<<"]}\n";
    return passed?0:1;
}
