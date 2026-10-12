#pragma once
#include "stdafx.h"
#include "FilterEngine.h"
#include "helpers/VSTPluginInstance.h"
#include "helpers/VSTPluginLibrary.h"
#include "outproc/OutProcControlEndpoint.h"
#include <fstream>
#include <chrono>
#include <thread>
#include <cmath>
#include <avrt.h>
#include <functional>
#pragma comment(lib,"avrt.lib")

inline int runEngineGenerationLiveAudit(int argc,wchar_t** argv,const std::function<void()>& tick={}){
    if(argc!=4 && argc!=5)return 2;
    const bool commercialView=argc==5 && std::wstring(argv[4])==L"--proq-native-view";
    const bool native=commercialView || argc==5 && std::wstring(argv[4])==L"--native-view";
    const bool proq=commercialView || argc==5 && std::wstring(argv[4])==L"--proq-band";if(argc==5 && !proq && !native)return 2;
    const std::wstring plugin=argv[1],directory=argv[2];
    const auto config=directory+L"\\engine-config.txt";
    const auto sid=EqApoControl::currentUserSid();
    const auto hostId=L"engine-transition-audit-"+std::to_wstring(GetCurrentProcessId());
    std::wstring savedChunks[2];
    if(native) {
        const auto name=plugin.substr(plugin.find_last_of(L"\\/")+1);
        if(commercialView ? (name!=L"FabFilter Pro-Q 3.dll" && name!=L"FabFilter Pro-Q 4.vst3") : name!=L"EqApoFixture.vst3")return 2;
        const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        auto library=VSTPluginLibrary::getInstance(plugin);if(library->initialize()<0)return 6;
        {
            VSTPluginInstance snapshot(library,2);if(!snapshot.initialize())return 6;
            snapshot.prepareForProcessing(48000,480);snapshot.startProcessing();
            double input[480]{},output[480]{};double* inputs[]={input,input};double* outputs[]={output,output};
            for(unsigned i=0;i<2;++i) {
                snapshot.writeToEffect(L"",proq?std::unordered_map<std::wstring,float>{{L"#0",1.f},{L"#1",1.f},{L"#3",i?.4f:.3f}}:std::unordered_map<std::wstring,float>{{L"#7",i?.75f:.25f}});
                snapshot.processDoubleReplacing(inputs,outputs,480);
                std::unordered_map<std::wstring,float> ignored;snapshot.readFromEffect(savedChunks[i],ignored);
                if(savedChunks[i].empty())return 6;
            }
            snapshot.stopProcessing();
        }
        if(SUCCEEDED(com))CoUninitialize();
    }
    auto write=[&](double gain){std::wofstream f(config);f<<L"OutProcVSTPlugin: Library \""<<plugin<<L"\" HostId "<<hostId<<L" GuiUserSID "<<sid;if(native)f<<L" ChunkData \""<<savedChunks[gain==(proq?.3:.25)?0:1]<<L"\"";else f<<(proq?L" #0 1 #1 1 #3 ":L" #7 ")<<gain;f<<L"\n";return f.good();};
    if(!write(proq?.3:.25))return 3;
    FilterEngine engine;engine.initialize(48000,2,2,2,3,480,config,true);
    DWORD task=0;HANDLE priority=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task);
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS);if(!timer)return 4;
    double a[480],b[480],x[480],y[480];double* in[]={a,b};double* out[]={x,y};
    for(unsigned f=0;f<480;++f)a[f]=b[f]=.1;
    double baseline=0,restored=0,editedAfterTransfer=0,transitionMin=1,transitionMax=-1;bool finite=true;
    const auto identity=EqApoControl::controlIdentity(plugin,L"",0);
    unsigned oldPid=0,newPid=0;bool guiOpened=false,guiTransferred=false,clickedAfterTransfer=false;
    std::uint64_t position=0;
    for(unsigned phase=0;phase<(native?3u:2u);++phase){
        if(phase==1){if(!write(proq?.4:.75))return 3;engine.loadConfig(config);}
        if(phase==2 && guiTransferred && commercialView)clickedAfterTransfer=EqApoControl::publishControl(hostId,identity,3,.3);
        if(phase==2 && guiTransferred && !commercialView) {
            struct Target {unsigned pid;HWND meter=nullptr;} target{newPid};
            EnumWindows([](HWND top,LPARAM value)->BOOL {
                auto* target=reinterpret_cast<Target*>(value);DWORD pid=0;GetWindowThreadProcessId(top,&pid);if(pid!=target->pid)return TRUE;
                EnumChildWindows(top,[](HWND child,LPARAM value)->BOOL {
                    auto* target=reinterpret_cast<Target*>(value);wchar_t name[128]{};GetClassNameW(child,name,128);
                    if(std::wstring(name)==L"EqApoFixtureMeter"){target->meter=child;return FALSE;}return TRUE;
                },value);return target->meter?FALSE:TRUE;
            },reinterpret_cast<LPARAM>(&target));
            if(target.meter)clickedAfterTransfer=PostMessageW(target.meter,WM_LBUTTONUP,0,MAKELPARAM(100,100))!=FALSE;
        }
        auto deadline=std::chrono::steady_clock::now();
        for(unsigned n=0;n<150;++n){
            if(proq)for(unsigned f=0;f<480;++f)a[f]=b[f]=.1*std::sin((position+f)*6.283185307179586*997/48000);
            if(tick)tick();
            engine.process(out,in,480);position+=480;
            if(native && n%10==0) {
                const auto view=EqApoControl::nativeViewControl(hostId,identity,phase?0:1);
                if(!phase && view.available && view.status==2){oldPid=view.pid;guiOpened=true;}
                if(phase==1 && view.available && view.status==2 && oldPid && view.pid!=oldPid){newPid=view.pid;guiTransferred=true;}
            }
            for(unsigned f=0;f<480;++f){finite&=std::isfinite(x[f]);if(n>=120)(phase==2?editedAfterTransfer:phase?restored:baseline)+=(proq?x[f]*x[f]:x[f])/14400.;
                if(phase==1 && n<40){transitionMin=(std::min)(transitionMin,x[f]);transitionMax=(std::max)(transitionMax,x[f]);}}
            deadline+=std::chrono::milliseconds(10);const auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-std::chrono::steady_clock::now()).count();
            if(remaining>0){LARGE_INTEGER due;due.QuadPart=-(std::max)(1LL,remaining/100);if(!SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE) || WaitForSingleObject(timer,INFINITE)!=WAIT_OBJECT_0)return 5;}
        }
    }
    CloseHandle(timer);if(priority)AvRevertMmThreadCharacteristics(priority);
    if(proq){baseline=std::sqrt(baseline);restored=std::sqrt(restored);editedAfterTransfer=std::sqrt(editedAfterTransfer);}
    const double expectedBefore=proq?.1/std::sqrt(2.)*std::pow(10.,-.6):.025,expectedAfter=proq?.1/std::sqrt(2.)*std::pow(10.,-.3):.075;
    const bool envelope=proq?(std::max)(std::abs(transitionMin),std::abs(transitionMax))<=.1*std::pow(10.,-.3)*1.05:transitionMin>=.024 && transitionMax<=.076;
    const bool levels=proq?(std::abs(baseline/expectedBefore-1)<.035 && std::abs(restored/expectedAfter-1)<.035)
        :(std::abs(baseline-.025)<.001 && std::abs(restored-.075)<.001);
    const bool passed=finite && levels && envelope && (!native || (guiOpened && guiTransferred && clickedAfterTransfer && (commercialView?std::abs(editedAfterTransfer/expectedBefore-1)<.035:std::abs(editedAfterTransfer-.1)<.001)));
    if(native)EqApoControl::nativeViewControl(hostId,identity,3);
    std::ofstream report(argv[3]);report<<"{\"commercial_native_view_trial\":"<<(commercialView?"true":"false")<<",\"post_transfer_edit_origin\":\""<<(commercialView?"control_endpoint":"native_fixture_click")<<"\",\"edited_after_transfer\":"<<editedAfterTransfer<<",\"clicked_after_transfer\":"<<(clickedAfterTransfer?"true":"false")<<",\"native_view_trial\":"<<(native?"true":"false")<<",\"gui_opened\":"<<(guiOpened?"true":"false")<<",\"gui_transferred\":"<<(guiTransferred?"true":"false")<<",\"old_pid\":"<<oldPid<<",\"new_pid\":"<<newPid<<",\"proq_band_trial\":"<<(proq?"true":"false")<<",\"baseline_measurement\":"<<baseline<<",\"restored_measurement\":"<<restored<<",\"expected_before\":"<<expectedBefore<<",\"expected_after\":"<<expectedAfter<<",\"transition_min\":"<<transitionMin<<",\"transition_max\":"<<transitionMax<<",\"status\":\""<<(passed?"PASS":"FAIL_HOST")<<"\",\"limits\":\"Actual FilterEngine live graph reload; native trials restore SDK chunks with GUI visible across independent worker reload; commercial post-transfer edit uses control endpoint, owned fixture uses native click; closed-view trials use numeric state; no installed audiodg certification\"}";
    return passed?0:1;
}
