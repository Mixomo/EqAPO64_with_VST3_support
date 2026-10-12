#include "stdafx.h"
#include "filters/OutProcVSTPluginFilter.h"
#include "filters/VSTPluginFilter.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <cmath>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <avrt.h>

struct LiveDiagnosticReader {
    std::atomic<bool> stopping{false};std::thread worker;
    LiveDiagnosticReader(OutProcVSTPluginFilter* filter,bool enabled) {
        if(enabled && filter)worker=std::thread([this,filter](){
            while(!stopping.load(std::memory_order_acquire))filter->getDiagnostics();
        });
    }
    void finish(){stopping.store(true,std::memory_order_release);if(worker.joinable())worker.join();}
    ~LiveDiagnosticReader(){finish();}
};

static bool killOwnHost(OutProcVSTPluginFilter* filter) {
    // The filter's pinned process handle identifies its own worker even when
    // that worker was launched by the isolated test broker rather than us.
    const auto diagnostic=filter?filter->getDiagnostics():OutProcVSTPluginFilter::Diagnostics{};
    if(diagnostic.hostAlive && diagnostic.hostPid){
        HANDLE owned=OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,diagnostic.hostPid);
        if(!owned)return false;const bool killed=TerminateProcess(owned,99)!=FALSE;CloseHandle(owned);return killed;
    }
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool killed=false;
    if(snapshot!=INVALID_HANDLE_VALUE && Process32FirstW(snapshot,&entry))do {
        if(entry.th32ParentProcessID==GetCurrentProcessId() && std::wstring(entry.szExeFile)==L"EqApoOutProcHost.exe") {
            HANDLE host=OpenProcess(PROCESS_TERMINATE,FALSE,entry.th32ProcessID);
            if(host) {killed=TerminateProcess(host,99)!=FALSE;CloseHandle(host);}break;
        }
    }while(Process32NextW(snapshot,&entry));
    if(snapshot!=INVALID_HANDLE_VALUE)CloseHandle(snapshot);return killed;
}

int wmain(int argc,wchar_t** argv) {
    if(argc<7)return 2;
    const std::wstring plugin=argv[1],outputPath=argv[2];
    const unsigned id=std::stoul(argv[3]);const double beforeValue=std::stod(argv[4]),afterValue=std::stod(argv[5]),expectedRatio=std::stod(argv[6]);
    std::unordered_map<std::wstring,float> parameters{{L"#"+std::to_wstring(id),float(beforeValue)}};
    bool recovery=false,inProcess=false,verifyClose=false,nativeViewDetach=false;std::wstring guiSession;
    unsigned rate=48000,frames=480;
    bool diagnosticStress=false,mmcssPacing=false;unsigned expectedSequence=1;
    for(int i=7;i<argc;) {
        if(std::wstring(argv[i])==L"--kill-after-edit") {recovery=true;++i;}
        else if(std::wstring(argv[i])==L"--verify-native-view-detach") {nativeViewDetach=true;++i;}
        else if(std::wstring(argv[i])==L"--in-process") {inProcess=true;++i;}
        else if(std::wstring(argv[i])==L"--verify-after-close") {verifyClose=true;++i;}
        else if(std::wstring(argv[i])==L"--gui-session" && i+1<argc) {guiSession=argv[i+1];i+=2;}
        else if(std::wstring(argv[i])==L"--rate" && i+1<argc) {rate=std::stoul(argv[i+1]);i+=2;}
        else if(std::wstring(argv[i])==L"--block" && i+1<argc) {frames=std::stoul(argv[i+1]);i+=2;}
        else if(std::wstring(argv[i])==L"--mmcss") {mmcssPacing=true;++i;}
        else if(std::wstring(argv[i])==L"--diagnostic-stress") {diagnosticStress=true;++i;}
        else if(std::wstring(argv[i])==L"--expected-sequence" && i+1<argc){expectedSequence=std::stoul(argv[i+1]);i+=2;}
        else {if(i+1>=argc)return 2;parameters[argv[i]]=std::stof(argv[i+1]);i+=2;}
    }
    if(nativeViewDetach && (recovery || inProcess || !guiSession.empty()))return 2;
    if(recovery && !guiSession.empty())return 2;
    if(inProcess && (guiSession.empty() || recovery))return 2;
    if(verifyClose && (guiSession.empty() || recovery))return 2;
    struct Pacing {
        HANDLE mmcss=nullptr;bool timer=false;
        explicit Pacing(bool enabled){if(enabled){timer=timeBeginPeriod(1)==TIMERR_NOERROR;DWORD index=0;mmcss=AvSetMmThreadCharacteristicsW(L"Pro Audio",&index);}}
        ~Pacing(){if(mmcss)AvRevertMmThreadCharacteristics(mmcss);if(timer)timeEndPeriod(1);}
    } pacing(mmcssPacing);
    std::unique_ptr<OutProcVSTPluginFilter> protectedFilter;
    std::unique_ptr<VSTPluginFilter> nativeFilter;
    if(inProcess) {
        auto library=VSTPluginLibrary::getInstance(plugin);if(library->initialize()<0)return 3;
        nativeFilter=std::make_unique<VSTPluginFilter>(library,L"",parameters,0,guiSession,EqApoControl::currentUserSid());
    } else protectedFilter=std::make_unique<OutProcVSTPluginFilter>(plugin,L"",parameters,guiSession,false,0,L"",guiSession.empty()?L"":EqApoControl::currentUserSid());
    IFilter& audioFilter=inProcess?static_cast<IFilter&>(*nativeFilter):static_cast<IFilter&>(*protectedFilter);
    if((rate!=44100 && rate!=48000 && rate!=96000) || !frames || frames>960)return 2;
    audioFilter.initialize(float(rate),frames,{L"L",L"R"});
    LiveDiagnosticReader observer(protectedFilter.get(),diagnosticStress);
    std::vector<double> a(frames),b(frames),x(frames),y(frames);double* input[]={a.data(),b.data()};double* output[]={x.data(),y.data()};
    unsigned long long position=0;double rms[3]{};bool published=false,finite=true,killed=false,rejectedPreserved=false,closedBeforeMeasurement=false;
    double producerMaxLateUs=0;unsigned lateOverQuantum=0;
    auto deadline=std::chrono::steady_clock::now();
    for(unsigned stage=0;stage<((recovery || verifyClose || nativeViewDetach)?3u:2u);++stage) {
        if(stage==1 && guiSession.empty()) {
            const auto diagnostics=protectedFilter->getDiagnostics();
            published=protectedFilter->publishParameter(diagnostics.parameterGeneration,id,afterValue,17);
            rejectedPreserved=!protectedFilter->publishParameter(diagnostics.parameterGeneration-1,id,beforeValue,0)
                && !protectedFilter->publishParameter(diagnostics.parameterGeneration,id,-1,0);
        }
        if(stage==1 && !guiSession.empty()) {std::ofstream ready(outputPath+L".ready");ready<<"PCM_BASELINE_COMPLETE\n";}
        if(stage==2 && recovery)killed=killOwnHost(protectedFilter.get());
        if(stage==2 && verifyClose) {std::ofstream marker(outputPath+L".edited");marker<<"GUI_EDIT_PCM_MEASURED\n";}
        const unsigned measureBlocks=(rate/2+frames-1)/frames;
        const unsigned stageFrames=nativeViewDetach?rate*3:guiSession.empty()?rate*3/2:((stage==0 || stage==2)?rate*2:rate*4);
        const unsigned blocks=(stageFrames+frames-1)/frames;
        for(unsigned block=0;block<blocks;++block) {
            if(stage==2 && verifyClose && block==blocks-measureBlocks)
                closedBeforeMeasurement=GetFileAttributesW((outputPath+L".gui-closed").c_str())!=INVALID_FILE_ATTRIBUTES;
            for(unsigned frame=0;frame<frames;++frame)a[frame]=b[frame]=.1*std::sin((position+frame)*6.283185307179586*997/rate);
            const double lateUs=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-deadline).count();
            producerMaxLateUs=(std::max)(producerMaxLateUs,lateUs);
            if(lateUs>1000000.*(std::max)(frames,unsigned((rate+99)/100))/rate)++lateOverQuantum;
            audioFilter.process(output,input,frames);
            for(unsigned frame=0;frame<frames;++frame) {
                finite &= std::isfinite(x[frame]) && std::isfinite(y[frame]);
                if(block>=blocks-measureBlocks)rms[stage]+=x[frame]*x[frame];
            }
            position+=frames;deadline+=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(double(frames)/rate));std::this_thread::sleep_until(deadline);
        }
        rms[stage]=std::sqrt(rms[stage]/double(measureBlocks*frames));
    }
    observer.finish();
    auto diagnostics=protectedFilter?protectedFilter->getDiagnostics():OutProcVSTPluginFilter::Diagnostics{};
    if(inProcess) {diagnostics.ready=true;diagnostics.hostAlive=true;diagnostics.parameterSequence=nativeFilter->getAppliedParameterSequence();
        diagnostics.parameterGeneration=nativeFilter->getParameterGeneration();diagnostics.parameterDrops=nativeFilter->getParameterDrops();}
    const double ratio=rms[1]/rms[0];
    if(!guiSession.empty()) {published=diagnostics.parameterSequence>0;rejectedPreserved=true;}
    const bool livePassed=(!nativeViewDetach || (rms[1]>0 && std::abs(rms[2]/rms[1]-1)<.03)) && published && rejectedPreserved && finite && diagnostics.hostAlive && diagnostics.ready && !diagnostics.parameterDrops && std::isfinite(ratio) && std::abs(ratio-expectedRatio)<.03*expectedRatio;
    const double recoveryRatio=recovery?rms[2]/rms[1]:1;
    const double closedRatio=(verifyClose || nativeViewDetach)?rms[2]/rms[1]:1;
    const bool passed=livePassed && (!verifyClose || (closedBeforeMeasurement && std::isfinite(closedRatio) && std::abs(closedRatio-1)<.03)) && (recovery?(killed && diagnostics.parameterGeneration>1 && std::abs(recoveryRatio-1)<.03):(!guiSession.empty()?diagnostics.parameterSequence>0:diagnostics.parameterSequence==expectedSequence));
    std::ofstream report(outputPath);report<<"{\"status\":\""<<(passed?"PASS":"PARAMETER_FAILURE")<<"\",\"sample_rate\":"<<rate<<",\"block_size\":"<<frames<<",\"processing_quantum\":"<<diagnostics.processingQuantum<<",\"latency_samples\":"<<audioFilter.getLatencySamples()<<",\"baseline_rms\":"<<rms[0]<<",\"edited_rms\":"<<rms[1]<<",\"ratio\":"<<ratio<<",\"expected_ratio\":"<<expectedRatio
        <<",\"published\":"<<(published?"true":"false")<<",\"parameter_generation\":"<<diagnostics.parameterGeneration<<",\"parameter_sequence\":"<<diagnostics.parameterSequence<<",\"parameter_drops\":"<<diagnostics.parameterDrops
        <<",\"native_dsp_view_trial\":"<<(nativeViewDetach?"true":"false")<<",\"mmcss_enabled\":"<<(pacing.mmcss?"true":"false")<<",\"producer_max_late_us\":"<<producerMaxLateUs<<",\"producer_late_over_quantum\":"<<lateOverQuantum
        <<",\"missing_blocks\":"<<diagnostics.missing<<",\"dropped_blocks\":"<<diagnostics.dropped<<",\"consumed_blocks\":"<<(inProcess?"null":std::to_string(diagnostics.consumed))<<",\"forced_host_crash\":"<<(killed?"true":"false")<<",\"recovered_rms\":"<<rms[2]<<",\"recovery_gain_ratio\":"<<recoveryRatio
        <<",\"gui_closed_before_measurement\":"<<(closedBeforeMeasurement?"true":"false")<<",\"after_gui_close_rms\":"<<((verifyClose || nativeViewDetach)?rms[2]:0)<<",\"after_gui_close_ratio\":"<<closedRatio
        <<",\"mode\":\""<<(inProcess?"IN_PROCESS":"OUT_OF_PROCESS")<<"\",\"limits\":\"Persistent filter PCM evidence; paired GUI report required for real GUI routing; installed audiodg not certified\"}\n";
    std::cout<<(passed?"PASS":"PARAMETER_FAILURE")<<" live_rms_ratio="<<ratio<<" sequence="<<diagnostics.parameterSequence<<'\n';return passed?0:1;
}
