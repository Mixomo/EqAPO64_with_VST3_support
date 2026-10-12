#include "stdafx.h"
#include "filters/OutProcVSTPluginFilter.h"
#include "outproc/OutProcControlEndpoint.h"
#include <thread>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
struct Panel {DWORD pid;HWND window=nullptr;double rms=-1;unsigned count=0;};
static BOOL CALLBACK children(HWND window,LPARAM data){
    auto& p=*reinterpret_cast<Panel*>(data);wchar_t title[256]{};GetWindowTextW(window,title,256);
    double value=0;if(swscanf_s(title,L"Fixture actual output RMS %lf",&value)==1)p.rms=value;
    return TRUE;
}
static BOOL CALLBACK top(HWND window,LPARAM data){
    auto& p=*reinterpret_cast<Panel*>(data);DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    wchar_t title[128]{};GetWindowTextW(window,title,128);
    if(pid==p.pid && std::wstring(title)==L"Equalizer APO - VST"){
        p.window=window;++p.count;EnumChildWindows(window,children,data);
    }return TRUE;
}
static Panel inspect(DWORD pid){Panel p{pid};EnumWindows(top,reinterpret_cast<LPARAM>(&p));return p;}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    try{
        GUID guid{};CoCreateGuid(&guid);wchar_t text[64]{};StringFromGUID2(guid,text,64);
        const std::wstring session=std::wstring(text).substr(1,36),path=argv[1],uid=L"EAA00001000000011000000190000001";
        const auto identity=EqApoControl::controlIdentity(path,uid,0);
        OutProcVSTPluginFilter filter(path,L"",{{L"#7",.25f}},session,false,0,uid,EqApoControl::currentUserSid());
        filter.initialize(48000,480,{L"L",L"R"});
        double source[480],left[480],right[480];double* input[]={source,source};double* output[]={left,right};
        for(unsigned i=0;i<480;++i)source[i]=.1*std::sin(i*6.283185307179586*997/48000.);
        auto feed=[&](unsigned blocks){for(unsigned i=0;i<blocks;++i){filter.process(output,input,480);std::this_thread::sleep_for(std::chrono::milliseconds(10));}};
        feed(150);EqApoControl::nativeViewControl(session,identity,1);feed(100);
        const auto pid=filter.getDiagnostics().hostPid;auto before=inspect(pid);
        if(before.count!=1 || before.rms<.001)throw std::runtime_error("LIVE_PANEL_METER");
        // Preserve the old filter/session while audio stops, as after module bypass.
        Sleep(1200);auto idle=inspect(pid);
        if(idle.count!=1 || idle.window!=before.window || idle.rms<0 || idle.rms>1e-6)throw std::runtime_error("RETAINED_GRAPH_METER_FROZEN");
        EqApoControl::nativeViewControl(session,identity,2);Sleep(600);auto hidden=inspect(pid);
        if(hidden.count!=1 || hidden.window!=before.window || IsWindowVisible(hidden.window) || hidden.rms>1e-6)throw std::runtime_error("HIDDEN_PANEL_IDLE");
        EqApoControl::nativeViewControl(session,identity,1);feed(100);auto restored=inspect(pid);
        if(restored.count!=1 || restored.window!=before.window || restored.rms<.001)throw std::runtime_error("PANEL_AUDIO_NOT_RESTORED");
        EqApoControl::nativeViewControl(session,identity,3);Sleep(300);
        if(IsWindow(before.window))throw std::runtime_error("PANEL_CLOSE_ORPHAN");
        std::cout<<"PASS retained graph meter: "<<before.rms<<" -> "<<idle.rms<<" -> "<<restored.rms<<"; same HWND/PID; hidden/close passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
