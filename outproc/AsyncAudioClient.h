#pragma once
#include "OutProcAudioProtocol.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace EqApoAsync {
// An input quantum is maxFrames, independent of callback size. Outputs live on
// a sample timeline at inputSample + 2*quantum. All storage is allocated by prepare.
class AudioClient {
public:
    void prepare(unsigned channels,unsigned quantum,unsigned sampleRate) {
        channelCount=channels; blockFrames=quantum; pipeline=2*quantum;
        capacity=pipeline+quantum+sampleRate*10+1;
        dry.assign(std::size_t(channels)*capacity,0.0);
        wet.assign(std::size_t(channels)*capacity,0.0);
        accumulated.assign(std::size_t(channels)*quantum,0.0);
        stamps.assign(capacity,~std::uint64_t(0));
        epochs.assign(capacity,~std::uint64_t(0));
        last.assign(channels,0.0);
        fadeStep=1.0/(std::max)(1u,sampleRate/100);
        position=sequence=0; accumulatedFrames=0; blend=0; pluginDelay=previousDelay=0; delayBlend=1;
        transitionReady=false;
    }
    unsigned transportLatency() const noexcept { return pipeline; }
    unsigned effectiveLatency() const noexcept { return pipeline+pluginDelay; }
    // Audio-owner snapshot; no mapping access or external wait at graph switch.
    bool readyForTransition() const noexcept { return transitionReady; }
    bool drySettled() const noexcept {return blend<=0 && delayBlend>=1;}
    void process(double** output,double** input,unsigned frames,OutProcAudioHeader* shared) noexcept {
        if(!channelCount || !blockFrames || !frames) return;
        const bool valid=shared && shared->magic==OUTPROC_AUDIO_MAGIC && shared->version==OUTPROC_AUDIO_VERSION &&
            shared->channelCount==channelCount && shared->maxFrames==blockFrames && shared->lastError.load(std::memory_order_acquire)!=4;
        if(valid) {
            const unsigned nextDelay=shared->pluginLatency.load(std::memory_order_acquire);
            if(nextDelay!=pluginDelay && nextDelay+pipeline+blockFrames<capacity) {
                previousDelay=pluginDelay; pluginDelay=nextDelay; delayBlend=0;
            }
            for(unsigned i=0;i<OUTPROC_AUDIO_SLOTS;++i) {
                auto* slot=OutProcSlot(shared,i);
                if(slot->state.load(std::memory_order_acquire)!=SLOT_OUTPUT) continue;
                bool finite=slot->status==OUTPROC_AUDIO_STATUS_OK && slot->frames==blockFrames && slot->epoch==shared->sessionEpoch.load(std::memory_order_acquire);
                const double* samples=OutProcSlotOutput(shared,slot);
                for(std::size_t n=0;finite && n<std::size_t(channelCount)*blockFrames;++n) finite=std::isfinite(samples[n]);
                if(finite) for(unsigned f=0;f<blockFrames;++f) {
                    const auto target=slot->streamSample+pipeline+f;
                    if(target<position || target>=position+capacity) continue;
                    const auto index=target%capacity;
                    for(unsigned c=0;c<channelCount;++c) wet[std::size_t(c)*capacity+index]=samples[std::size_t(c)*blockFrames+f];
                    stamps[index]=target;epochs[index]=slot->epoch;
                }
                slot->state.store(SLOT_FREE,std::memory_order_release);
            }
        }
        bool missed=false,allWet=true;
        for(unsigned f=0;f<frames;++f,++position) {
            const auto index=position%capacity;
            bool ready=valid && shared->ready.load(std::memory_order_acquire) && stamps[index]==position && epochs[index]==shared->sessionEpoch.load(std::memory_order_acquire) && delayBlend>=1 && shared->pluginLatency.load(std::memory_order_relaxed)+pipeline+blockFrames<capacity;
            const double targetBlend=ready ? 1.0 : 0.0;
            blend+=(std::max)(-fadeStep,(std::min)(fadeStep,targetBlend-blend));
            allWet &= ready && blend>=1;
            delayBlend=(std::min)(1.0,delayBlend+fadeStep);
            missed|=!ready && position>=pipeline+pluginDelay;
            for(unsigned c=0;c<channelCount;++c) {
                const auto base=std::size_t(c)*capacity;
                const double sample=std::isfinite(input[c][f]) ? input[c][f] : 0.0;
                accumulated[std::size_t(c)*blockFrames+accumulatedFrames]=sample;
                dry[base+index]=sample;
                const unsigned currentLatency=pipeline+pluginDelay,oldLatency=pipeline+previousDelay;
                const double newDry=position>=currentLatency ? dry[base+(position-currentLatency)%capacity] : 0;
                const double oldDry=position>=oldLatency ? dry[base+(position-oldLatency)%capacity] : 0;
                const double delayed=oldDry+(newDry-oldDry)*delayBlend;
                // A late/dead host has no current wet sample. Fade the last safe
                // endpoint into aligned dry, never read stale shared memory.
                const double wetSample=ready ? wet[base+index] : last[c];
                const double result=delayed+(wetSample-delayed)*blend;
                output[c][f]=std::isfinite(result) ? result : delayed;
                if(ready) last[c]=wetSample;
            }
            if(++accumulatedFrames==blockFrames) {
                if(valid) {
                    OutProcAudioSlot* free=nullptr;
                    for(unsigned i=0;i<OUTPROC_AUDIO_SLOTS;++i) {
                        auto* slot=OutProcSlot(shared,i);
                        if(slot->state.load(std::memory_order_acquire)==SLOT_FREE) { free=slot; break; }
                    }
                    if(free) {
                        memcpy(OutProcSlotInput(free),accumulated.data(),accumulated.size()*sizeof(double));
                        free->frames=blockFrames; free->sequence=++sequence; free->streamSample=position+1-blockFrames;
                        free->epoch=shared->sessionEpoch.load(std::memory_order_acquire);
                        free->state.store(SLOT_INPUT,std::memory_order_release);
                        shared->produced.fetch_add(1,std::memory_order_relaxed);
                    } else shared->overruns.fetch_add(1,std::memory_order_relaxed);
                }
                accumulatedFrames=0;
            }
        }
        if(valid && missed) shared->underruns.fetch_add(1,std::memory_order_relaxed);
        const bool quarantined=shared && shared->lastError.load(std::memory_order_acquire)==4;
        transitionReady=allWet || (quarantined && blend<=0 && delayBlend>=1);
    }
private:
    unsigned channelCount=0,blockFrames=0,pipeline=0,capacity=0,accumulatedFrames=0;
    unsigned pluginDelay=0,previousDelay=0;
    std::uint64_t position=0,sequence=0;
    double blend=0,fadeStep=1,delayBlend=1;
    bool transitionReady=false;
    std::vector<double> dry,wet,accumulated,last;
    std::vector<std::uint64_t> stamps,epochs;
};
}
