#include "stdafx.h"
#include "FilterEngine.h"
#include "filters/VSTPluginFilter.h"
#include "filters/OutProcVSTPluginFilter.h"
#include "CrashDiagnostics.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <cmath>
#include <filesystem>
#include <avrt.h>
#pragma comment(lib,"avrt.lib")

// Sacrificial real-filter graph test. Audio owns graph processing; the control
// thread constructs/destructs configurations outside callbacks. The harness
// clock sleeps between callbacks, never inside the production callback.
struct Command {
    FilterConfiguration* next=nullptr;
    double expectedRms=0;
    std::atomic<bool> done{false};
    unsigned blocks=0,silentBlocks=0,invalidSamples=0;
    bool transitionCompleted=false;
    double rms=0,minimumRms=1e9,maxProducerLateUs=0;
};
class AudioClock {
public:
    explicit AudioClock(FilterConfiguration* initial):current(initial),worker([this]{run();}){}
    ~AudioClock(){stop.store(true);if(worker.joinable())worker.join();}
    void perform(Command& command) {
        request.store(&command,std::memory_order_release);
        const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!command.done.load(std::memory_order_acquire)) {
            if(std::chrono::steady_clock::now()>limit) {
                // Keep the stack command alive until its audio consumer exits.
                // A stuck third-party callback is terminated by the outer
                // owned-process timeout, rather than creating a test UAF.
                stop.store(true,std::memory_order_release);worker.join();
                throw std::runtime_error("AUDIO_COMMAND_TIMEOUT");
            }
            Sleep(1); // Controller only; no wait inside graph/filter processing.
        }
    }
private:
    std::atomic<Command*> request{nullptr};std::atomic<bool> stop{false};
    FilterConfiguration* current;std::thread worker;
    void run() {
        DWORD task=0;HANDLE priority=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task);
        HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS);
        if(!timer){stop.store(true);return;}
        double a[480],b[480],x[480],y[480];double* input[]={a,b};double* output[]={x,y};
        std::uint64_t position=0;Command* active=nullptr;unsigned transition=0;
        auto deadline=std::chrono::steady_clock::now();
        while(!stop.load(std::memory_order_acquire)) {
            if(!active) {active=request.exchange(nullptr,std::memory_order_acq_rel);transition=0;}
            if(active)active->maxProducerLateUs=(std::max)(active->maxProducerLateUs,double(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-deadline).count()));
            for(unsigned f=0;f<480;++f)a[f]=b[f]=.1*std::sin((position+f)*6.283185307179586*997/48000.);
            current->read(input,480);current->process(480);
            if(active && active->next) {
                active->next->read(input,480);active->next->process(480);
                transition=current->doTransition(active->next,480,transition,480);
            }
            current->write(output,480);
            if(active) {
                double energy=0;
                for(unsigned f=0;f<480;++f) {
                    if(!std::isfinite(x[f]) || !std::isfinite(y[f]))++active->invalidSamples;
                    energy+=x[f]*x[f];
                    if(active->blocks>=45)active->rms+=x[f]*x[f];
                }
                const double level=std::sqrt(energy/480);
                active->minimumRms=(std::min)(active->minimumRms,level);
                if(level<1e-8)++active->silentBlocks;
                if(++active->blocks==50) {
                    active->transitionCompleted=!active->next || transition>=480;
                    if(active->next && active->transitionCompleted)current=active->next;
                    active->rms=std::sqrt(active->rms/2400);
                    auto* completed=active;active=nullptr;
                    completed->done.store(true,std::memory_order_release);
                }
            }
            position+=480;deadline+=std::chrono::milliseconds(10);
            const auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-std::chrono::steady_clock::now()).count();
            if(remaining>0){LARGE_INTEGER due;due.QuadPart=-(std::max)(1LL,remaining/100);
                if(!SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE) || WaitForSingleObject(timer,INFINITE)!=WAIT_OBJECT_0){stop.store(true);break;}}
        }
        CloseHandle(timer);if(priority)AvRevertMmThreadCharacteristics(priority);
    }
};

int wmain(int argc,wchar_t** argv) {
    if(argc<6)return 2;
    SetUnhandledExceptionFilter(smokeCrashDiagnostic);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const std::wstring plugin=std::filesystem::absolute(argv[1]).wstring(),mode=argv[2],destination=argv[3];
    const unsigned cycles=std::stoul(argv[4]);const double gain=std::stod(argv[5]);
    if((mode!=L"INPROC" && mode!=L"OUTPROC") || cycles<1 || cycles>10)return 2;
    std::unordered_map<std::wstring,float> values;
    for(int i=6;i+1<argc;i+=2)values[argv[i]]=std::stof(argv[i+1]);
    std::ofstream report(destination);unsigned phases=0,failures=0,reloads=0,loadUnload=0,bypasses=0;
    const auto start=std::chrono::steady_clock::now();
    try {
        auto library=VSTPluginLibrary::getInstance(plugin);if(library->initialize()<0)throw std::runtime_error("MODULE_LOAD");
        const std::wstring scratch=destination+L".empty.txt";{std::ofstream empty(scratch);}
        FilterEngine engine;engine.initialize(48000,2,2,2,3,480,scratch);
        auto dry=std::make_unique<FilterConfiguration>(&engine,std::vector<FilterInfo*>{},2);
        struct Graph {std::unique_ptr<FilterConfiguration> configuration;OutProcVSTPluginFilter* protectedFilter=nullptr;};
        auto create=[&]() {
            IFilter* filter=nullptr;Graph graph;
            if(mode==L"INPROC")filter=new(MemoryHelper::alloc(sizeof(VSTPluginFilter)))VSTPluginFilter(library,L"",values);
            else {graph.protectedFilter=new(MemoryHelper::alloc(sizeof(OutProcVSTPluginFilter)))OutProcVSTPluginFilter(plugin,L"",values,L"",false,0,library->isVST3()?library->getVST3ClassUID(0):L"");filter=graph.protectedFilter;}
            filter->initialize(48000,480,{L"L",L"R"});
            auto* info=static_cast<FilterInfo*>(MemoryHelper::alloc(sizeof(FilterInfo)));
            info->filter=filter;info->inPlace=false;info->inChannelCount=info->outChannelCount=2;
            info->inChannels=static_cast<size_t*>(MemoryHelper::alloc(2*sizeof(size_t)));
            info->outChannels=static_cast<size_t*>(MemoryHelper::alloc(2*sizeof(size_t)));
            info->inChannels[0]=info->outChannels[0]=0;info->inChannels[1]=info->outChannels[1]=1;
            graph.configuration=std::make_unique<FilterConfiguration>(&engine,std::vector<FilterInfo*>{info},2);return graph;
        };
        Graph wet;
        // Clock is destroyed before the configurations it references.
        AudioClock audio(dry.get());
        // Establish dry history before the first latency-changing load. There
        // are no past input samples at cold start for a delay line to return.
        Command prime;audio.perform(prime);
        wet=create();
        auto snapshot=[&](OutProcVSTPluginFilter* filter) {
            auto result=filter->getDiagnostics();unsigned retries=0;
            // Unavailable is a refused concurrent snapshot, not a zero-valued
            // observation of a dead host. Retry only in this test controller.
            while(!result.available && retries<20) {++retries;Sleep(1);result=filter->getDiagnostics();}
            if(retries)std::cout<<"diagnostic_snapshot_retries="<<retries<<std::endl;
            return result;
        };
        auto phase=[&](const char* name,FilterConfiguration* next,Graph* target,double expected) {
            Command command;command.next=next;command.expectedRms=expected*.1/std::sqrt(2.);
            const auto before=target && target->protectedFilter?snapshot(target->protectedFilter).consumed:0;
            audio.perform(command);
            // Never release the graph still owned by the callback after an incomplete fade.
            if(!command.transitionCompleted)throw std::runtime_error("GRAPH_TRANSITION_NOT_COMPLETE");
            bool dsp=true;
            if(target && target->protectedFilter) {
                const auto diagnostics=snapshot(target->protectedFilter);
                dsp=diagnostics.available && diagnostics.hostAlive && diagnostics.ready && !diagnostics.error && diagnostics.consumed>=before+10;
                std::cout<<"transport available="<<diagnostics.available<<" before="<<before<<" consumed="<<diagnostics.consumed<<" ready="<<diagnostics.ready<<" error="<<diagnostics.error<<" missing="<<diagnostics.missing<<" dropped="<<diagnostics.dropped<<" alive="<<diagnostics.hostAlive<<std::endl;
            }
            const bool passed=dsp && !command.invalidSamples && !command.silentBlocks &&
                std::isfinite(command.rms) && std::abs(command.rms-command.expectedRms)<.035*command.expectedRms;
            ++phases;if(!passed)++failures;
            std::cout<<name<<" rms="<<command.rms<<" expected="<<command.expectedRms<<" producer_max_late_us="<<command.maxProducerLateUs<<" silent="<<command.silentBlocks<<" dsp="<<dsp<<" "<<(passed?"PASS":"FAIL")<<std::endl;
        };
        phase("initial_load",wet.configuration.get(),&wet,gain);
        for(unsigned cycle=0;cycle<cycles;++cycle) {
            phase("bypass",dry.get(),nullptr,1);
            phase("unbypass",wet.configuration.get(),&wet,gain);++bypasses;
            auto replacement=create();phase("reload",replacement.configuration.get(),&replacement,gain);
            wet=std::move(replacement);++reloads;
            phase("unload",dry.get(),nullptr,1);wet.configuration.reset();
            wet=create();phase("load",wet.configuration.get(),&wet,gain);++loadUnload;
        }
        // Move callback ownership away before destroying the last wet graph.
        phase("final_unload",dry.get(),nullptr,1);
        DeleteFileW(scratch.c_str());
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        report<<"{\"status\":\""<<(failures?"FAIL_HOST":"PASS")<<"\",\"cycles\":"<<cycles<<",\"bypass_unbypass\":"<<bypasses<<",\"reload\":"<<reloads<<",\"load_unload\":"<<loadUnload<<",\"phases\":"<<phases<<",\"failed_phases\":"<<failures<<",\"seconds\":"<<seconds<<",\"gui_open\":false,\"limits\":\"Actual production FilterConfiguration crossfades and filters, independent continuously paced PCM thread; RMS oracle tolerance 3.5%, no zero blocks; no Editor UI or installed audiodg proof\"}\n";
        return failures?1:0;
    }catch(const std::exception& error){report<<"{\"status\":\"FAIL_HOST\",\"reason\":\""<<error.what()<<"\"}\n";return 1;}
}
