#include "stdafx.h"
#include "filters/OutProcVSTPluginFilter.h"
#include "helpers/VSTPluginLibrary.h"
#include "helpers/VSTPluginInstance.h"
#include <chrono>
#include <iostream>
#include <fstream>
#include <cmath>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <avrt.h>

int wmain(int argc,wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(argc<4) return 2;
    const std::wstring path=argv[1], report=argv[2];
    const unsigned duration=std::stoul(argv[3]);
    if(duration<1 || duration>60) {std::cerr<<"Smoke duration must be 1..60 seconds\n";return 2;}
    bool analyzer=false,oracle=false,faults=false;std::wstring gainId;double oracleGain=1;
    for(int i=4;i<argc;++i) {
        const std::wstring option=argv[i];
        if(option==L"--analysis")analyzer=true;
        else if(option==L"--faults")faults=true;
        else if(option==L"--oracle" && i+2<argc) {oracle=true;gainId=argv[++i];oracleGain=std::stod(argv[++i]);}
    }
    timeBeginPeriod(1);
    DWORD mmcssIndex=0;
    HANDLE mmcss=AvSetMmThreadCharacteristicsW(L"Pro Audio",&mmcssIndex);
    if(mmcss)AvSetMmThreadPriority(mmcss,AVRT_PRIORITY_HIGH);
    HANDLE pacingTimer=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS);
    if(!pacingTimer)pacingTimer=CreateWaitableTimerW(nullptr,FALSE,nullptr);
    std::ofstream result(report);
    try {
        auto library=VSTPluginLibrary::getInstance(path);
        if(library->initialize()<0) throw std::runtime_error("MODULE_LOAD");
        VSTPluginInstance probe(library,4);
        if(!probe.initialize()) throw std::runtime_error("INITIALIZE");
        std::wstring state;std::unordered_map<std::wstring,float> parameters;
        probe.readFromEffect(state,parameters);
        if(oracle)parameters[gainId]=float(oracleGain);
        const unsigned pluginLatency=(std::max)(0,probe.getInitialDelay());
        const unsigned rate=48000,quantum=480;
        const unsigned expectedLatency=pluginLatency+(analyzer?0:2*quantum);
        OutProcVSTPluginFilter filter(path,state,parameters,L"",analyzer,0,
            library->isVST3()?library->getVST3ClassUID(0):L"");
        filter.initialize(float(rate),quantum,{L"L",L"R"});
        // Startup is outside the measured processing interval.
        Sleep(1000);
        std::vector<double> input0(quantum),input1(quantum),output0(quantum),output1(quantum);
        double* input[]={input0.data(),input1.data()};double* output[]={output0.data(),output1.data()};
        std::vector<double> durations;durations.reserve(size_t(duration)*100+100);
        unsigned long long position=0,nonzero=0,oracleChecks=0,oracleFailures=0;unsigned kills=0;
        double lastFault=-100;
        double maxDiscontinuity=0,last=0;
        const auto started=std::chrono::steady_clock::now();auto deadline=started;
        do {
            const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            if(faults && kills<4 && elapsed>=3+5*kills) {
                // The unified worker can be a child of the user broker, not
                // this harness. Inject only into this exact filter session.
                const auto diagnostic=filter.getDiagnostics();
                if(diagnostic.hostAlive && diagnostic.hostPid) {
                    HANDLE child=OpenProcess(PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,diagnostic.hostPid);
                    if(child) {
                        wchar_t executable[32768]{};DWORD length=32768;
                        const bool named=QueryFullProcessImageNameW(child,0,executable,&length)
                            && std::wstring(executable).substr(std::wstring(executable).find_last_of(L"\\/")+1)==L"EqApoOutProcHost.exe";
                        const auto current=filter.getDiagnostics();
                        if(named && current.hostAlive && current.hostPid==diagnostic.hostPid && current.epoch==diagnostic.epoch
                            && TerminateProcess(child,99)){++kills;lastFault=elapsed;}
                        CloseHandle(child);
                    }
                }
            }
            for(unsigned f=0;f<quantum;++f) input0[f]=input1[f]=0.1*std::sin((position+f)*6.283185307179586*997/rate);
            auto before=std::chrono::steady_clock::now();
            filter.process(output,input,quantum);
            durations.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-before).count());
            for(unsigned f=0;f<quantum;++f) {
                if(!std::isfinite(output0[f]) || !std::isfinite(output1[f])) throw std::runtime_error("NONFINITE_AUDIO");
                if(std::abs(output0[f])>1e-10) ++nonzero;
                if(position+f>expectedLatency+rate) maxDiscontinuity=(std::max)(maxDiscontinuity,std::abs(output0[f]-last));
                last=output0[f];
                if(oracle && position+f>expectedLatency+rate && elapsed-lastFault>2 && (!faults || kills<4)) {
                    const auto at=position+f-(analyzer ? pluginLatency : filter.getLatencySamples());
                    const double expected=oracleGain*0.1*std::sin(at*6.283185307179586*997/rate);
                    ++oracleChecks;if(std::abs(output0[f]-expected)>2e-6)++oracleFailures;
                }
            }
            position+=quantum;
            if(!analyzer) {
                deadline+=std::chrono::milliseconds(10);
                const auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-std::chrono::steady_clock::now()).count();
                if(remaining>0) {
                    if(!pacingTimer)throw std::runtime_error("PACING_TIMER_CREATE");
                    LARGE_INTEGER due;due.QuadPart=-(std::max)(1LL,remaining/100);
                    if(!SetWaitableTimer(pacingTimer,&due,0,nullptr,nullptr,FALSE)
                        || WaitForSingleObject(pacingTimer,INFINITE)!=WAIT_OBJECT_0)
                        throw std::runtime_error("PACING_TIMER_WAIT");
                }
            }
        } while(std::chrono::steady_clock::now()-started<std::chrono::seconds(duration));
        std::sort(durations.begin(),durations.end());
        if(!nonzero) throw std::runtime_error("NO_AUDIO");
        if(oracle && (!oracleChecks || oracleFailures)) {std::cerr<<"oracle_checks="<<oracleChecks<<" failures="<<oracleFailures<<" latency="<<filter.getLatencySamples()<<" max_delta="<<maxDiscontinuity<<"\n";throw std::runtime_error("WET_TIMELINE_ORACLE");}
        if(faults && kills!=4)throw std::runtime_error("FAULT_INJECTION_COUNT");
        const double p99=durations[size_t((durations.size()-1)*0.99)],max=durations.back();
        result<<"{\"status\":\"PASS\",\"mode\":\""<<(analyzer?"analysis_clone":"inherited_outproc")
            <<"\",\"duration_seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()
            <<",\"callbacks\":"<<durations.size()<<",\"frames\":"<<position<<",\"declared_latency_samples\":"<<(analyzer ? pluginLatency : filter.getLatencySamples())
            <<",\"callback_p99_us\":"<<p99<<",\"callback_max_us\":"<<max
            <<",\"max_adjacent_sample_delta\":"<<maxDiscontinuity<<",\"oracle_samples\":"<<oracleChecks<<",\"oracle_failures\":"<<oracleFailures<<",\"forced_host_crashes\":"<<kills
            <<",\"limits\":[\"The optional oracle covers the declared linear gain fixture only; does not certify audiodg, arbitrary-plugin wet/dry identity, graph transitions or deadline compliance under system load\"]}\n";
        if(pacingTimer)CloseHandle(pacingTimer);
        if(mmcss)AvRevertMmThreadCharacteristics(mmcss);
        timeEndPeriod(1);
        std::cout<<"PASS callbacks="<<durations.size()<<" p99_us="<<p99<<" max_us="<<max<<'\n';return 0;
    } catch(const std::exception& e) {
        if(pacingTimer)CloseHandle(pacingTimer);
        if(mmcss)AvRevertMmThreadCharacteristics(mmcss);
        timeEndPeriod(1);
        result<<"{\"status\":\"FAIL\",\"reason\":\""<<e.what()<<"\"}\n";
        std::cerr<<e.what()<<'\n';return 1;
    }
}
