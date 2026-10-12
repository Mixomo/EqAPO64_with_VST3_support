#include "stdafx.h"
#include "FilterEngine.h"
#include "outproc/OutProcControlEndpoint.h"
#include <fstream>
#include <chrono>
#include <thread>
#include <iostream>
#include <cmath>

static HWND nativeWindow(unsigned pid) {
    struct Search {unsigned pid;HWND window=nullptr;} search{pid};
    EnumWindows([](HWND window,LPARAM raw)->BOOL {
        auto& search=*reinterpret_cast<Search*>(raw);DWORD owner=0;
        GetWindowThreadProcessId(window,&owner);wchar_t title[128]{};GetWindowTextW(window,title,128);
        if(owner==search.pid && IsWindowVisible(window) && std::wstring(title)==L"Equalizer APO - VST")search.window=window;
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));return search.window;
}

int wmain(int argc,wchar_t** argv) {
    if(argc!=5 && argc!=6)return 2;
    std::ifstream source(argv[1],std::ios::binary);
    const std::string active((std::istreambuf_iterator<char>(source)),{});
    if(active.empty())return 2;
    std::string restored=active;
    if(argc==6){std::ifstream state(argv[5],std::ios::binary);restored.assign(std::istreambuf_iterator<char>(state),{});if(restored.empty())return 2;}
    auto write=[&](bool bypass,bool reactivation=false){std::ofstream file(argv[2],std::ios::binary);if(bypass)file<<"# ";file<<(reactivation?restored:active);return file.good();};
    if(!write(false))return 3;
    FilterEngine engine;engine.initialize(44100,2,2,2,3,441,argv[2],true);
    const auto identity=std::stoull(argv[4]);
    double input[441],left[441],right[441];double* in[]={input,input};double* out[]={left,right};
    unsigned pids[3]{};long long readyMs[3]={-1,-1,-1};bool finite=true;
    HWND views[3]{};double stereoPower[3]{};unsigned samples[3]{};
    std::uint64_t position=0;
    for(unsigned phase=0;phase<3;++phase) {
        if(phase){if(!write(phase==1,phase==2))return 3;engine.loadConfig(argv[2]);}
        const auto start=std::chrono::steady_clock::now();auto deadline=start;
        for(unsigned block=0;block<1500;++block) {
            for(unsigned f=0;f<441;++f)input[f]=.003*std::sin((position+f)*6.283185307179586*997/44100.);
            engine.process(out,in,441);position+=441;
            for(auto v:left)finite &= std::isfinite(v);
            const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
            if(block%10==0) {
                auto view=EqApoControl::nativeViewControl(argv[3],identity);
                const bool ready=phase==1 ? !engine.hasPendingConfiguration() : view.available && view.status>=1 && view.status<=3 && !engine.hasPendingConfiguration();
                if(ready && readyMs[phase]<0){readyMs[phase]=ms;pids[phase]=view.pid;}
                if(phase==0 && ready)EqApoControl::nativeViewControl(argv[3],identity,1);
            }
            if(readyMs[phase]>=0 && ms>=readyMs[phase]+(phase==1?11000:2000))break;
            if(readyMs[phase]>=0 && ms>readyMs[phase]+500 && phase!=1) {
                for(unsigned f=0;f<441;++f){const auto difference=left[f]-right[f];stereoPower[phase]+=difference*difference;++samples[phase];}
            }
            deadline+=std::chrono::milliseconds(10);std::this_thread::sleep_until(deadline);
        }
        views[phase]=nativeWindow(pids[phase]);
    }
    const auto view=EqApoControl::nativeViewControl(argv[3],identity);
    const auto beforeRms=samples[0]?std::sqrt(stereoPower[0]/samples[0]):0.;
    const auto afterRms=samples[2]?std::sqrt(stereoPower[2]/samples[2]):0.;
    const bool retainedWindow=views[0] && views[0]==views[2];
    const bool passed=finite && readyMs[0]>=0 && readyMs[1]>=0 && readyMs[1]<1000 && readyMs[2]>=0 && readyMs[2]<1000 && pids[0] && pids[0]==pids[2] && view.status==2 && retainedWindow && beforeRms>1e-6 && afterRms>1e-6;
    EqApoControl::nativeViewControl(argv[3],identity,3);
    std::cout<<"{\"initial_ms\":"<<readyMs[0]<<",\"bypass_ms\":"<<readyMs[1]<<",\"reactivation_ms\":"<<readyMs[2]
        <<",\"initial_pid\":"<<pids[0]<<",\"reactivated_pid\":"<<pids[2]<<",\"native_view_status\":"<<view.status
        <<",\"same_gui_hwnd\":"<<(retainedWindow?"true":"false")<<",\"processed_stereo_rms_before\":"<<beforeRms<<",\"processed_stereo_rms_after\":"<<afterRms
        <<",\"finite\":"<<(finite?"true":"false")<<",\"passed\":"<<(passed?"true":"false")<<"}\n";
    return passed?0:1;
}
