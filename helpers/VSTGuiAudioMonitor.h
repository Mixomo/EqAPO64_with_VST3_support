#pragma once
#include "VSTPluginInstance.h"
#include "outproc/OutProcControlEndpoint.h"
#include <thread>
#include <array>
#include <chrono>

// Vendor GUI instance only: processes actual pre-filter input for meters and
// adaptive Learn controls. Mirror output is discarded and never mixed into APO.
class VSTGuiAudioMonitor {
    VSTPluginInstance* effect;
    EqApoControl::MonitorReader reader;
    std::atomic<bool> stopping{false};
    std::atomic<std::uint64_t> framesProcessed{0};
    std::thread worker;
    std::vector<float> input,output;
    std::vector<double> doubleInput,doubleOutput;
    unsigned channels=0;
    double rate=0;
    bool waitingOnInput=true;
public:
    VSTGuiAudioMonitor(VSTPluginInstance* instance,const std::wstring& session,std::uint64_t identity):effect(instance) {
        if(!effect || effect->numInputs()>64 || effect->numOutputs()>64 || !reader.connect(session,identity))return;
        waitingOnInput=false;
        channels=reader.channels();rate=reader.sampleRate();
        constexpr unsigned stride=EqApoControl::MonitorMailbox::framesLimit;
        input.resize(64*stride);output.resize(64*stride);
        doubleInput.resize(64*stride);doubleOutput.resize(64*stride);
        effect->setUsedChannelCount(int(channels));effect->prepareForProcessing(float(rate),stride);
        if(!effect->isProcessingValid())return;
        effect->startProcessing();
        worker=std::thread([this,stride]() {
            CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            std::array<float*,64> in{},out{};std::array<double*,64> din{},dout{};
            for(unsigned c=0;c<64;++c){in[c]=input.data()+c*stride;out[c]=output.data()+c*stride;din[c]=doubleInput.data()+c*stride;dout[c]=doubleOutput.data()+c*stride;}
            while(!stopping.load(std::memory_order_acquire)) {
                if(!reader.refresh() || reader.sampleRate()!=rate || reader.channels()!=channels){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
                // Do not consume input while a preset snapshot owns the mirror.
                std::unique_lock<std::recursive_mutex> lock(effect->editorMirrorMutex(),std::try_to_lock);
                const unsigned frames=lock.owns_lock()?reader.consume(input.data()):0;
                if(frames) {
                    if(effect->canDoubleReplacing()) {
                        for(unsigned c=0;c<channels;++c)for(unsigned f=0;f<frames;++f)din[c][f]=in[c][f];
                        effect->processDoubleReplacing(din.data(),dout.data(),int(frames));
                    }else effect->process(in.data(),out.data(),int(frames));
                    framesProcessed.fetch_add(frames,std::memory_order_relaxed);
                }
                if(lock.owns_lock())lock.unlock();
                if(!frames)std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            CoUninitialize();
        });
    }
    bool active()const{return worker.joinable();}
    bool waitingForInput()const{return waitingOnInput;}
    std::uint64_t processedFrames()const{return framesProcessed.load();}
    ~VSTGuiAudioMonitor(){stopping.store(true,std::memory_order_release);if(worker.joinable()){worker.join();effect->stopProcessing();}}
};
