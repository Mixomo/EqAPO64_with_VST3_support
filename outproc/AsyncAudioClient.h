#pragma once
#include "OutProcAudioProtocol.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace EqApoAsync {
// An input quantum is maxFrames, independent of callback size. Outputs live on
// a sample timeline at inputSample + pipeline. All storage is allocated by prepare.
//
// Buffered mode gives the worker one quantum of wall-clock time after a block
// is complete. When every callback has the same size and divides the quantum,
// blocks complete exactly at a callback boundary and pipeline = 2*quantum -
// callback (one quantum when callbacks equal the quantum). Irregular callback
// sizes, or a block that arrives after its first sample was due, raise the
// pipeline toward the conservative 2*quantum; it never decreases.
//
// Hybrid mode (optional, per plug-in) adds no transport latency: when each
// callback is exactly one quantum, the block is published and the audio thread
// waits a bounded time for the worker's answer within the same callback. A
// missed answer plays aligned dry for that block; repeated misses switch this
// instance to buffered mode permanently (until the graph is rebuilt).
class AudioClient {
public:
    // Audio-thread rendezvous supplied by the owner. wait() wakes the worker
    // and returns when the slot holds output or the owner's time budget ends.
    struct SyncLink {
        void* context=nullptr;
        void (*wait)(void* context,OutProcAudioSlot* slot) noexcept=nullptr;
    };
    static constexpr unsigned syncMissLimit=3;
    void prepare(unsigned channels,unsigned quantum,unsigned sampleRate) {
        tenMsFrames=(std::max)(1u,(sampleRate+99)/100);
        channelCount=channels; blockFrames=quantum; maxPipeline=pipeline=2*quantum;
        previousPipeline=pipeline; callbackFrames=0; pipelineGeneration=0; steadyWet=false;
        capacity=maxPipeline+quantum+sampleRate*10+1;
        dry.assign(std::size_t(channels)*capacity,0.0);
        wet.assign(std::size_t(channels)*capacity,0.0);
        accumulated.assign(std::size_t(channels)*quantum,0.0);
        stamps.assign(capacity,~std::uint64_t(0));
        epochs.assign(capacity,~std::uint64_t(0));
        generations.assign(capacity,~0u);
        last.assign(channels,0.0);
        fadeStep=1.0/(std::max)(1u,sampleRate/100);
        missWindow=std::uint64_t(sampleRate)*10;
        position=sequence=0; accumulatedFrames=0; blend=0; pluginDelay=previousDelay=0; delayBlend=1;
        transitionReady=false; syncOn=false; syncLeft=false; misses=0; firstMiss=0; totalMisses=0;
    }
    // Request hybrid mode before the first process() call.
    void enableSync(SyncLink link) noexcept {
        sync=link;
        // Hybrid blocks may be far below 10 ms (Voicemeeter over ASIO). Its
        // buffered ceiling still gives the worker ~10 ms after a block; the
        // 10 s history in capacity covers this extra quantum.
        if(link.wait) pipeline=previousPipeline=maxPipeline=
            (std::max)(2*blockFrames,blockFrames+(tenMsFrames+blockFrames-1)/blockFrames*blockFrames);
    }
    unsigned transportLatency() const noexcept { return pipeline; }
    unsigned effectiveLatency() const noexcept { return pipeline+pluginDelay; }
    bool syncActive() const noexcept { return syncOn; }
    // True once hybrid mode was requested but this instance left it.
    bool syncFellBack() const noexcept { return syncLeft; }
    unsigned syncMissCount() const noexcept { return totalMisses; }
    // Audio-owner snapshot; no mapping access or external wait at graph switch.
    bool readyForTransition() const noexcept { return transitionReady; }
    bool drySettled() const noexcept {return blend<=0 && delayBlend>=1;}
    void process(double** output,double** input,unsigned frames,OutProcAudioHeader* shared) noexcept {
        if(!channelCount || !blockFrames || !frames) return;
        if(!callbackFrames) {
            // First callback, before any block exists: choose the timeline the
            // engine reads through getLatencySamples() after this call.
            callbackFrames=frames;
            if(sync.wait && frames==blockFrames) {
                syncOn=true; pipeline=previousPipeline=0;
            } else {
                syncLeft=sync.wait!=nullptr;
                if(frames<=blockFrames && blockFrames%frames==0)
                    pipeline=previousPipeline=2*blockFrames-frames;
            }
        } else if(frames!=callbackFrames) {
            // Fragments no longer end on block boundaries.
            if(syncOn) leaveSync(maxPipeline);
            else if(pipeline<maxPipeline) raisePipeline(maxPipeline);
        }
        const bool valid=shared && shared->magic==OUTPROC_AUDIO_MAGIC && shared->version==OUTPROC_AUDIO_VERSION &&
            shared->channelCount==channelCount && shared->maxFrames==blockFrames && shared->lastError.load(std::memory_order_acquire)!=4;
        if(valid) {
            const unsigned nextDelay=shared->pluginLatency.load(std::memory_order_acquire);
            if(nextDelay!=pluginDelay && nextDelay+maxPipeline+blockFrames<capacity) {
                previousDelay=pluginDelay; pluginDelay=nextDelay; previousPipeline=pipeline; delayBlend=0;
            }
            receive(shared);
        }
        // Hybrid: this callback is one whole block. Publish it first and wait
        // for the answer, so the output loop below finds it at pipeline 0.
        bool submitted=false;
        if(valid && syncOn && accumulatedFrames==0 && frames==blockFrames) {
            for(unsigned c=0;c<channelCount;++c) for(unsigned f=0;f<frames;++f)
                accumulated[std::size_t(c)*blockFrames+f]=std::isfinite(input[c][f]) ? input[c][f] : 0.0;
            submitted=true;
            OutProcAudioSlot* slot=publish(shared,position);
            const bool live=shared->ready.load(std::memory_order_acquire)!=0;
            // A ready worker that never signals slots predates hybrid mode
            // (an older broker still running): every wait would time out.
            const bool workerSignals=(*static_cast<volatile std::uint32_t*>(&shared->asyncTransport) & OUTPROC_TRANSPORT_SLOT_SIGNAL)!=0;
            if(live && !workerSignals) leaveSync(bufferedFallback());
            bool answered=false;
            if(slot && live && workerSignals && syncOn) {
                sync.wait(sync.context,slot);
                answered=slot->state.load(std::memory_order_acquire)==SLOT_OUTPUT;
                if(answered) receive(shared);
            }
            // Only a stream that was already serving wet can miss a deadline;
            // a warming or restarting worker is not a timing failure.
            if(syncOn && live && workerSignals && steadyWet && !answered) noteSyncMiss();
        }
        bool missed=false,allWet=true;
        for(unsigned f=0;f<frames;++f,++position) {
            const auto index=position%capacity;
            bool ready=valid && shared->ready.load(std::memory_order_acquire) && stamps[index]==position && epochs[index]==shared->sessionEpoch.load(std::memory_order_acquire) && generations[index]==pipelineGeneration && delayBlend>=1 && shared->pluginLatency.load(std::memory_order_relaxed)+maxPipeline+blockFrames<capacity;
            const double targetBlend=ready ? 1.0 : 0.0;
            blend+=(std::max)(-fadeStep,(std::min)(fadeStep,targetBlend-blend));
            allWet &= ready && blend>=1;
            delayBlend=(std::min)(1.0,delayBlend+fadeStep);
            missed|=!ready && position>=pipeline+pluginDelay;
            for(unsigned c=0;c<channelCount;++c) {
                const auto base=std::size_t(c)*capacity;
                const double sample=std::isfinite(input[c][f]) ? input[c][f] : 0.0;
                if(!submitted) accumulated[std::size_t(c)*blockFrames+accumulatedFrames]=sample;
                dry[base+index]=sample;
                const unsigned currentLatency=pipeline+pluginDelay,oldLatency=previousPipeline+previousDelay;
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
            if(!submitted && ++accumulatedFrames==blockFrames) {
                if(valid) publish(shared,position+1-blockFrames);
                accumulatedFrames=0;
            }
        }
        if(valid && missed) shared->underruns.fetch_add(1,std::memory_order_relaxed);
        const bool quarantined=shared && shared->lastError.load(std::memory_order_acquire)==4;
        transitionReady=allWet || (quarantined && blend<=0 && delayBlend>=1);
        // A late block arrives after the misses it caused, so a miss alone
        // keeps this; only a stream that is not (yet) serving wet clears it.
        if(!valid || !shared->ready.load(std::memory_order_acquire)) steadyWet=false;
        else if(allWet) steadyWet=true;
    }
private:
    void receive(OutProcAudioHeader* shared) noexcept {
        for(unsigned i=0;i<OUTPROC_AUDIO_SLOTS;++i) {
            auto* slot=OutProcSlot(shared,i);
            if(slot->state.load(std::memory_order_acquire)!=SLOT_OUTPUT) continue;
            bool finite=slot->status==OUTPROC_AUDIO_STATUS_OK && slot->frames==blockFrames && slot->epoch==shared->sessionEpoch.load(std::memory_order_acquire);
            const double* samples=OutProcSlotOutput(shared,slot);
            for(std::size_t n=0;finite && n<std::size_t(channelCount)*blockFrames;++n) finite=std::isfinite(samples[n]);
            if(!syncOn && finite && steadyWet && slot->streamSample+pipeline<position && pipeline<maxPipeline
                && shared->ready.load(std::memory_order_acquire)) {
                // The worker missed a deadline while audio was steadily wet.
                raisePipeline((std::min)(maxPipeline,pipeline+callbackFrames));
            }
            if(finite) for(unsigned f=0;f<blockFrames;++f) {
                const auto target=slot->streamSample+pipeline+f;
                if(target<position || target>=position+capacity) continue;
                const auto index=target%capacity;
                for(unsigned c=0;c<channelCount;++c) wet[std::size_t(c)*capacity+index]=samples[std::size_t(c)*blockFrames+f];
                stamps[index]=target;epochs[index]=slot->epoch;generations[index]=pipelineGeneration;
            }
            slot->state.store(SLOT_FREE,std::memory_order_release);
        }
    }
    OutProcAudioSlot* publish(OutProcAudioHeader* shared,std::uint64_t streamSample) noexcept {
        OutProcAudioSlot* free=nullptr;
        for(unsigned i=0;i<OUTPROC_AUDIO_SLOTS;++i) {
            auto* slot=OutProcSlot(shared,i);
            if(slot->state.load(std::memory_order_acquire)==SLOT_FREE) { free=slot; break; }
        }
        if(!free) { shared->overruns.fetch_add(1,std::memory_order_relaxed); return nullptr; }
        memcpy(OutProcSlotInput(free),accumulated.data(),accumulated.size()*sizeof(double));
        free->frames=blockFrames; free->sequence=++sequence; free->streamSample=streamSample;
        free->epoch=shared->sessionEpoch.load(std::memory_order_acquire);
        free->state.store(SLOT_INPUT,std::memory_order_release);
        shared->produced.fetch_add(1,std::memory_order_relaxed);
        return free;
    }
    void noteSyncMiss() noexcept {
        ++totalMisses;
        if(!misses || position-firstMiss>missWindow) { misses=1; firstMiss=position; }
        else ++misses;
        // Repeated misses mean this system or plug-in cannot meet the
        // in-callback deadline: keep sound stable with the buffered timeline.
        if(misses>=syncMissLimit) leaveSync(bufferedFallback());
    }
    // Hybrid blocks of 10 ms or more fall back to the one-quantum timeline;
    // smaller blocks need the ~10 ms ceiling to stay stable.
    unsigned bufferedFallback() const noexcept {
        return blockFrames>=tenMsFrames ? 2*blockFrames-callbackFrames : maxPipeline;
    }
    void leaveSync(unsigned next) noexcept {
        syncOn=false; syncLeft=true;
        raisePipeline((std::max)(next,blockFrames));
    }
    void raisePipeline(unsigned next) noexcept {
        if(next<=pipeline) return;
        // Same dry crossfade as a plug-in latency change. Wet samples stamped
        // for the previous timeline are invalidated by generation, not cleared.
        previousPipeline=pipeline; previousDelay=pluginDelay; pipeline=next; delayBlend=0;
        ++pipelineGeneration; steadyWet=false;
    }
    SyncLink sync;
    unsigned channelCount=0,blockFrames=0,pipeline=0,previousPipeline=0,maxPipeline=0,capacity=0,accumulatedFrames=0;
    unsigned callbackFrames=0,pipelineGeneration=0,tenMsFrames=1;
    bool steadyWet=false,syncOn=false,syncLeft=false;
    unsigned misses=0,totalMisses=0;
    std::uint64_t firstMiss=0,missWindow=0;
    unsigned pluginDelay=0,previousDelay=0;
    std::uint64_t position=0,sequence=0;
    double blend=0,fadeStep=1,delayBlend=1;
    bool transitionReady=false;
    std::vector<double> dry,wet,accumulated,last;
    std::vector<std::uint64_t> stamps,epochs;
    std::vector<unsigned> generations;
};
}
