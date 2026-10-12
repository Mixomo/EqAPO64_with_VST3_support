#include "stdafx.h"
#include "helpers/VSTPluginLibrary.h"
#include "helpers/VSTPluginInstance.h"
#include "filters/VSTPluginFilter.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <memory>
#include <vector>
#include <stdexcept>
#include "CrashDiagnostics.h"

static void require(bool value,const char* cause) {if(!value) throw std::runtime_error(cause);}
static void pump(unsigned milliseconds) {
    const auto end=GetTickCount64()+milliseconds;
    do { MSG message; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);} Sleep(1); } while(GetTickCount64()<end);
}
int wmain(int argc,wchar_t** argv) {
    std::cout.setf(std::ios::unitbuf);
    SetUnhandledExceptionFilter(smokeCrashDiagnostic);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::wstring path,outPath;bool gui=false;unsigned iterations=70;
    for(int i=1;i<argc;++i) {
        if(std::wstring(argv[i])==L"--plugin" && i+1<argc) path=argv[++i];
        else if(std::wstring(argv[i])==L"--out" && i+1<argc) outPath=argv[++i];
        else if(std::wstring(argv[i])==L"--gui") gui=true;
        else if(std::wstring(argv[i])==L"--iterations" && i+1<argc) iterations=std::stoul(argv[++i]);
    }
    if(path.empty()) return 2;
    std::ofstream result(outPath,std::ios::trunc);int testCases=0,guiCycles=0;
    try {
        auto library=VSTPluginLibrary::getInstance(path);
        std::cout<<"STAGE module_load\n";
        require(library && library->initialize()>=0,"MODULE_LOAD_OR_DEPENDENCY");
        auto probe=std::make_unique<VSTPluginInstance>(library,4);
        std::cout<<"STAGE component_initialize\n";
        require(probe->initialize(),"INITIALIZE");
        const bool supports64=probe->canDoubleReplacing();
        const auto uid=library->isVST3() ? library->getVST3ClassUID(0) : L"";
        std::wstring initialState;std::unordered_map<std::wstring,float> initialParameters;
        probe->readFromEffect(initialState,initialParameters);
        probe.reset();
        std::cout<<"STAGE audio_matrix\n";
        for(bool preferDouble:{false,true}) for(unsigned rate:{44100u,48000u,96000u}) for(unsigned requestedChannels:{1u,2u,6u,8u}) {
            if(preferDouble && !supports64) continue;
            auto instance=std::make_unique<VSTPluginInstance>(library,4);
            require(instance->initialize(),"RELOAD_INITIALIZE");
            instance->setUsedChannelCount(requestedChannels);
            instance->writeToEffect(initialState,initialParameters);
            instance->prepareForProcessing(float(rate),960,preferDouble);
            instance->startProcessing();
            const int inputs=instance->numInputs(),outputs=instance->numOutputs();
            require(inputs>=0 && outputs>0 && inputs<=64 && outputs<=64,"BUS_NEGOTIATION");
            std::vector<std::vector<double>> input(inputs,std::vector<double>(960)),output(outputs,std::vector<double>(960));
            std::vector<std::vector<float>> floatInput(inputs,std::vector<float>(960)),floatOutput(outputs,std::vector<float>(960));
            std::vector<double*> ip(inputs),op(outputs);std::vector<float*> fp(inputs),fo(outputs);
            for(int c=0;c<inputs;++c) {ip[c]=input[c].data();fp[c]=floatInput[c].data();}
            for(int c=0;c<outputs;++c) {op[c]=output[c].data();fo[c]=floatOutput[c].data();}
            std::uint64_t position=0;
            const unsigned blocks[]={64,480,128,960,32,512,256};
            for(unsigned iteration=0;iteration<iterations;++iteration) {
                const unsigned frames=blocks[iteration%7];
                for(int c=0;c<inputs;++c) for(unsigned f=0;f<frames;++f) {
                    const double sample=0.1*std::sin((position+f)*6.283185307179586*997/rate);
                    input[c][f]=sample;floatInput[c][f]=float(sample);
                }
                if(iteration%14==0 && !initialParameters.empty()) {
                    auto automation=initialParameters;automation.begin()->second=float((iteration%70)/70.0);
                    instance->writeToEffect(L"",automation);
                }
                require(instance->serviceRestartRequests(),"RESTART_COMPONENT");
                if(instance->canDoubleReplacing()) instance->processDoubleReplacing(ip.data(),op.data(),frames);
                else instance->processReplacing(fp.data(),fo.data(),frames);
                require(instance->isProcessingValid(),"PROCESS_RESULT");
                for(int c=0;c<outputs;++c) for(unsigned f=0;f<frames;++f)
                    require(std::isfinite(instance->canDoubleReplacing() ? output[c][f] : double(floatOutput[c][f])),"NONFINITE_AUDIO");
                position+=frames;
            }
            instance->stopProcessing();
            instance->writeToEffect(initialState,initialParameters);
            std::wstring restored;std::unordered_map<std::wstring,float> restoredParams;
            instance->readFromEffect(restored,restoredParams);
            for(const auto& parameter:initialParameters) {
                auto it=restoredParams.find(parameter.first);
                require(it!=restoredParams.end() && std::abs(it->second-parameter.second)<1e-5,"STATE_PARAMETER_ROUNDTRIP");
            }
            std::cout<<"PASS rate="<<rate<<" requested="<<requestedChannels<<" negotiated="<<inputs<<"->"<<outputs<<" precision="<<(instance->canDoubleReplacing() ? 64 : 32)<<'\n';
            ++testCases;
        }
        if(gui) {
            std::cout<<"STAGE gui_cycles\n";
            auto instance=std::make_unique<VSTPluginInstance>(library,4);
            require(instance->initialize(),"GUI_INITIALIZE");
            instance->prepareForProcessing(48000,512);
            HWND window=CreateWindowExW(0,L"STATIC",L"EqAPO VST GUI smoke",WS_OVERLAPPEDWINDOW|WS_VISIBLE,80,80,600,400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            require(window!=nullptr,"GUI_HOST_WINDOW");
            for(unsigned i=0;i<50;++i) {
                short w=400,h=300;
                if(!instance->startEditing(window,&w,&h)) break;
                instance->setEditorScale(float(GetDpiForWindow(window))/96.0f);
                SetWindowPos(window,nullptr,0,0,w+32,h+64,SWP_NOMOVE|SWP_NOZORDER);
                pump(30);instance->doIdle();instance->stopEditing();++guiCycles;
            }
            DestroyWindow(window);
        }
        result<<"{\"status\":\"PASS\",\"audio_cases\":"<<testCases<<",\"gui_cycles\":"<<guiCycles
            <<",\"iterations_per_case\":"<<iterations<<",\"float64_supported\":"<<(supports64 ? "true" : "false")<<",\"class_uid\":\""<<std::string(uid.begin(),uid.end())
            <<"\",\"checks\":[\"initialize\",\"variable_blocks\",\"finite_audio\",\"parameter_updates\",\"state_parameter_roundtrip\",\"unload_reload\"],\"limits\":[\"Does not certify installed audiodg audio, all bus routes, licensing dialogs, live monitor transitions, exact arbitrary-plugin state, leaks, or crash recovery\"]}\n";
        std::cout<<"PASS plugin smoke cases="<<testCases<<" gui="<<guiCycles<<'\n';
        result.close();CoUninitialize();return 0;
    } catch(const std::exception& e) {
        result<<"{\"status\":\"FAIL\",\"stage\":\""<<e.what()<<"\",\"audio_cases\":"<<testCases<<"}\n";
        std::cerr<<"FAIL "<<e.what()<<'\n';result.close();CoUninitialize();return 1;
    }
}
