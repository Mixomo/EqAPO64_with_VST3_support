#include "stdafx.h"
#include "filters/VSTPluginFilter.h"
#include "filters/OutProcVSTPluginFilter.h"
#include "helpers/VSTPluginInstance.h"
#include "CrashDiagnostics.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <memory>
#include <map>
#include <chrono>

struct Measurement { double rms=0,dryError=0;unsigned blocks=0;unsigned long long samples=0,processedDuringMeasurement=0; };
struct DiagnosticReader {
    std::atomic<bool> stopping{false};std::thread worker;
    DiagnosticReader(OutProcVSTPluginFilter* filter,bool enabled) {
        if(enabled && filter)worker=std::thread([this,filter](){
            while(!stopping.load(std::memory_order_acquire))filter->getDiagnostics();
        });
    }
    void finish(){stopping.store(true,std::memory_order_release);if(worker.joinable())worker.join();}
    ~DiagnosticReader(){finish();}
};
static std::string jsonText(const std::wstring& value) {
    const int size=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    std::string utf8(size,0),escaped;
    WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),utf8.data(),size,nullptr,nullptr);
    for(unsigned char ch:utf8) {
        if(ch=='"' || ch=='\\')escaped+='\\';
        if(ch<32) {char sequence[7];snprintf(sequence,sizeof(sequence),"\\u%04x",ch);escaped+=sequence;}
        else escaped+=ch;
    }
    return '"'+escaped+'"';
}
static Measurement measure(IFilter& filter,unsigned rate,unsigned block,OutProcVSTPluginFilter* protectedFilter=nullptr,double level=.1) {
    std::vector<double> a(block),b(block),x(block),y(block);
    double* input[]={a.data(),b.data()};double* output[]={x.data(),y.data()};
    Measurement result;auto deadline=std::chrono::steady_clock::now();std::uint64_t firstConsumed=0;
    const unsigned warmupBlocks=(rate+block-1)/block;
    const unsigned measuredBlocks=(rate*2/5+block-1)/block;
    result.blocks=measuredBlocks;
    const auto period=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(double(block)/rate));
    for(unsigned n=0;n<warmupBlocks+measuredBlocks;++n) {
        if(n==warmupBlocks && protectedFilter)firstConsumed=protectedFilter->getDiagnostics().consumed;
        for(unsigned i=0;i<block;++i) a[i]=b[i]=level*std::sin((n*block+i)*6.283185307179586*997/rate);
        filter.process(output,input,block);
        if(n>=warmupBlocks) for(unsigned i=0;i<block;++i) {
            if(!std::isfinite(x[i]) || !std::isfinite(y[i]))throw std::runtime_error("NONFINITE_AUDIO");
            const auto position=static_cast<long long>(n*block+i)-filter.getLatencySamples();
            const double dry=position>=0 ? level*std::sin(position*6.283185307179586*997/rate) : 0;
            result.rms+=x[i]*x[i];result.dryError+=(x[i]-dry)*(x[i]-dry);++result.samples;
        }
        deadline+=period;std::this_thread::sleep_until(deadline);
    }
    if(protectedFilter) {
        const auto consumed=protectedFilter->getDiagnostics().consumed;
        result.processedDuringMeasurement=consumed>=firstConsumed?consumed-firstConsumed:0;
    }
    result.rms=std::sqrt(result.rms/result.samples);result.dryError=std::sqrt(result.dryError/result.samples);return result;
}
int wmain(int argc,wchar_t** argv) {
    SetUnhandledExceptionFilter(smokeCrashDiagnostic);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    bool apartmentMta=false;
    for(int i=1;i<argc;++i)if(std::wstring(argv[i])==L"--apartment-mta")apartmentMta=true;
    CoInitializeEx(nullptr,apartmentMta?COINIT_MULTITHREADED:COINIT_APARTMENTTHREADED);
    std::wstring path,mode,out,baselineState,editedState,choicesTitle;std::unordered_map<std::wstring,float> baselineEdits,edits;bool requireDelta=false,legacyParameters=false,metadataOnly=false;
    unsigned rate=48000,block=480;
    double signalLevel=.1;
    bool diagnosticStress=false;
    for(int i=1;i<argc;++i) {
        const std::wstring key=argv[i];
        if(key==L"--plugin" && i+1<argc)path=argv[++i];
        else if(key==L"--mode" && i+1<argc)mode=argv[++i];
        else if(key==L"--out" && i+1<argc)out=argv[++i];
        else if(key==L"--baseline-state" && i+1<argc)baselineState=argv[++i];
        else if(key==L"--edited-state" && i+1<argc)editedState=argv[++i];
        else if((key==L"--baseline-set" || key==L"--edit-set") && i+2<argc) {
            const std::wstring id=argv[++i];const float value=std::stof(argv[++i]);
            (key==L"--baseline-set" ? baselineEdits : edits)[id]=value;
        } else if(key==L"--require-delta")requireDelta=true;
        else if(key==L"--legacy-parameters")legacyParameters=true;
        else if(key==L"--metadata-only")metadataOnly=true;
        else if(key==L"--choices-title" && i+1<argc)choicesTitle=argv[++i];
        else if(key==L"--rate" && i+1<argc)rate=std::stoul(argv[++i]);
        else if(key==L"--block" && i+1<argc)block=std::stoul(argv[++i]);
        else if(key==L"--signal-level" && i+1<argc)signalLevel=std::stod(argv[++i]);
        else if(key==L"--diagnostic-stress")diagnosticStress=true;
    }
    if(path.empty() || out.empty() || (mode!=L"INPROC" && mode!=L"OUTPROC"))return 2;
    if(!std::isfinite(signalLevel) || signalLevel<=0 || signalLevel>.1)return 2;
    if((rate!=44100 && rate!=48000 && rate!=96000) || (block!=32 && block!=64 && block!=128 && block!=256 && block!=480 && block!=512 && block!=960))return 2;
    std::ofstream formatReport(out+L".format.json");
    formatReport<<"{\"sample_rate\":"<<rate<<",\"block_size\":"<<block<<",\"sample_domain\":\"Host double PCM; plugin SDK precision remains automatic\"}";formatReport.close();
    std::ofstream report(out);try {
        auto library=VSTPluginLibrary::getInstance(path);
        if(library->initialize()<0)throw std::runtime_error("MODULE_LOAD");
        auto probe=std::make_unique<VSTPluginInstance>(library,4);
        if(!probe->initialize())throw std::runtime_error("INITIALIZE");
        probe->setUsedChannelCount(2);probe->prepareForProcessing(float(rate),int(block));
        if(probe->numInputs()==0 || probe->numOutputs()==0)throw std::runtime_error("UNSUPPORTED_IO");
        std::wstring state;std::unordered_map<std::wstring,float> parameters;probe->readFromEffect(state,parameters);
        const auto metadata=probe->getParameterMetadata();
        if(legacyParameters && !library->isVST3()) {
            std::unordered_map<std::wstring,float> legacy;
            for(const auto& entry:metadata)legacy[entry.title]=float(entry.normalizedValue);
            parameters=std::move(legacy);
        }
        std::ofstream parametersReport(out+L".parameters.json");parametersReport<<'[';
        for(size_t i=0;i<metadata.size();++i) {
            if(i)parametersReport<<',';
            const auto& parameter=metadata[i];
            parametersReport<<"{\"id\":"<<parameter.id<<",\"title\":"<<jsonText(parameter.title)<<",\"normalized\":"<<parameter.normalizedValue<<",\"flags\":"<<parameter.flags<<",\"step_count\":"<<parameter.stepCount;
            if(metadataOnly && library->isVST3() && parameter.stepCount>0 && parameter.stepCount<=64) {
                parametersReport<<",\"choices\":[";
                for(int step=0;step<=parameter.stepCount;++step) {
                    if(step)parametersReport<<',';
                    const double normalized=double(step)/parameter.stepCount;
                    parametersReport<<"{\"normalized\":"<<normalized<<",\"text\":"<<jsonText(probe->getParameterValueText(parameter.id,normalized))<<'}';
                }
                parametersReport<<']';
            }
            if(metadataOnly && !library->isVST3() && parameter.title==choicesTitle) {
                // This sacrificial scanner instance is not processing audio.
                // Query actual VST2 display labels after each bounded edit;
                // do not borrow a different version's discrete enum mapping.
                std::map<std::wstring,std::pair<double,double>> ranges;
                for(int step=0;step<=100;++step) {
                    const double value=double(step)/100;
                    probe->writeToEffect(L"",{{L"#"+std::to_wstring(parameter.id),float(value)}});
                    const auto label=probe->getCurrentParameterText(parameter.id);
                    if(label.empty())continue;
                    const auto found=ranges.find(label);
                    if(found==ranges.end())ranges.emplace(label,std::make_pair(value,value));
                    else found->second.second=value;
                }
                probe->writeToEffect(L"",parameters);
                parametersReport<<",\"choices\":[";bool first=true;
                for(const auto& range:ranges) {
                    if(!first)parametersReport<<',';first=false;
                    parametersReport<<"{\"normalized\":"<<(range.second.first+range.second.second)/2
                                    <<",\"text\":"<<jsonText(range.first)<<'}';
                }
                parametersReport<<']';
            }
            parametersReport<<'}';
        }
        parametersReport<<']';parametersReport.close();
        if(metadataOnly){report<<"{\"status\":\"METADATA_ONLY\"}";return 0;}
        const int inputs=probe->numInputs(),outputs=probe->numOutputs(),uniqueId=probe->uniqueID();const bool doubleSupported=probe->canDoubleReplacing();probe.reset();
        Measurement measurements[2];unsigned latencies[2]{};bool transportReady=true;
        OutProcVSTPluginFilter::Diagnostics transport[2];
        for(unsigned variant=0;variant<2;++variant) {
            auto values=parameters;auto variantState=state;
            const auto statePath=variant?editedState:baselineState;
            if(!statePath.empty()) {
                OutProcVSTConfig snapshot;
                if(!OutProcReadVSTConfig(statePath,snapshot) || _wcsicmp(snapshot.libraryPath.c_str(),path.c_str())!=0 || snapshot.vst3ClassIndex!=0)
                    throw std::runtime_error("GUI_SNAPSHOT_IDENTITY");
                variantState=snapshot.chunkData;values=snapshot.paramMap;
            }
            for(const auto& value:baselineEdits)values[value.first]=value.second;
            if(variant)for(const auto& value:edits)values[value.first]=value.second;
            std::unique_ptr<IFilter> filter;
            if(mode==L"INPROC")filter=std::make_unique<VSTPluginFilter>(library,variantState,values,0);
            else filter=std::make_unique<OutProcVSTPluginFilter>(path,variantState,values,L"",false,0,library->isVST3()?library->getVST3ClassUID(0):L"");
            filter->initialize(float(rate),block,{L"L",L"R"});
            DiagnosticReader observer(mode==L"OUTPROC"?static_cast<OutProcVSTPluginFilter*>(filter.get()):nullptr,diagnosticStress);
            measurements[variant]=measure(*filter,rate,block,mode==L"OUTPROC"?static_cast<OutProcVSTPluginFilter*>(filter.get()):nullptr,signalLevel);latencies[variant]=filter->getLatencySamples();
            observer.finish();
            if(mode==L"OUTPROC") {
                transport[variant]=static_cast<OutProcVSTPluginFilter*>(filter.get())->getDiagnostics();
                const auto& snapshot=transport[variant];
                const unsigned requiredBlocks=(std::min)(20u,(std::max)(1u,measurements[variant].blocks-1));
                transportReady &= snapshot.available && snapshot.hostAlive && snapshot.ready && !snapshot.error && measurements[variant].processedDuringMeasurement>=requiredBlocks;
            }
        }
        const double delta=std::abs(measurements[1].rms-measurements[0].rms);
        const bool passed=transportReady && (!requireDelta || (delta>1e-4 && measurements[1].rms>1e-9));
        const char* classification=!transportReady?"HOST_DSP_NOT_READY":!passed?"PARAMETER_FAILURE":measurements[1].rms<=1e-9?"OBSERVED_SILENCE":"PASS";
        std::ofstream transportReport(out+L".transport.json");transportReport<<'[';
        for(unsigned i=0;i<2;++i) {
            if(i)transportReport<<',';const auto& snapshot=transport[i];
            transportReport<<"{\"available\":"<<(snapshot.available?"true":"false")<<",\"processing_quantum\":"<<snapshot.processingQuantum<<",\"host_alive\":"<<(snapshot.hostAlive?"true":"false")
                <<",\"ready\":"<<snapshot.ready<<",\"error\":"<<snapshot.error<<",\"heartbeat\":"<<snapshot.heartbeat
                <<",\"consumed\":"<<snapshot.consumed<<",\"produced\":"<<snapshot.produced<<",\"missing\":"<<snapshot.missing
                <<",\"dropped\":"<<snapshot.dropped<<",\"epoch\":"<<snapshot.epoch<<",\"processed_measurement_blocks\":"<<measurements[i].processedDuringMeasurement<<'}';
        }transportReport<<']';transportReport.close();
        report<<"{\"status\":\""<<classification<<"\",\"mode\":\""<<std::string(mode.begin(),mode.end())<<"\",\"sample_rate\":"<<rate<<",\"block_size\":"<<block<<",\"inputs\":"<<inputs<<",\"outputs\":"<<outputs<<",\"unique_id\":"<<uniqueId<<",\"float64\":"<<(doubleSupported?"true":"false")<<",\"baseline_rms\":"<<measurements[0].rms<<",\"edited_rms\":"<<measurements[1].rms<<",\"rms_delta\":"<<delta<<",\"baseline_dry_error\":"<<measurements[0].dryError<<",\"edited_dry_error\":"<<measurements[1].dryError<<",\"baseline_latency\":"<<latencies[0]<<",\"edited_latency\":"<<latencies[1]<<",\"gui_open\":false,\"limits\":\"Actual filter PCM at reported sample rate/block, two load/unload cycles; no installed audiodg or GUI parameter delivery certification\"}\n";
        std::cout<<classification<<" baseline_rms="<<measurements[0].rms<<" edited_rms="<<measurements[1].rms<<" delta="<<delta<<'\n';return passed?0:1;
    } catch(const std::exception& error) {report<<"{\"status\":\"FAIL\",\"reason\":\""<<error.what()<<"\"}\n";std::cerr<<error.what()<<'\n';return 1;}
}
