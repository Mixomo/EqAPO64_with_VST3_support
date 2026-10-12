#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>

namespace EqApoControl {
// Optional input mirror for a separate vendor editor. Its output never enters
// the audio graph. All slots have exclusive ownership; the APO never waits.
struct MonitorMailbox {
    static constexpr unsigned magicValue=0x4D4E5045,versionValue=1;
    static constexpr unsigned channelsLimit=64,framesLimit=1024,slotCount=8;
    const unsigned magic=magicValue,version=versionValue;
    const std::uint64_t generation;
    double sampleRate=48000;
    unsigned channels=2;
    std::atomic<unsigned> enabled{0};
    std::atomic<std::uint64_t> dropped{0};
    struct Slot {
        std::atomic<unsigned> state{0}; // free / writing / ready / reading
        std::uint64_t sequence=0;
        unsigned frames=0;
        float input[channelsLimit*framesLimit];
    } blocks[slotCount];
    unsigned producer=0,consumer=0;
    std::uint64_t nextSequence=0;
    explicit MonitorMailbox(std::uint64_t gen):generation(gen){}
    void publish(double** input,unsigned count) noexcept {
        if(!enabled.load(std::memory_order_relaxed) || !channels || channels>channelsLimit)return;
        for(unsigned offset=0;offset<count;offset+=framesLimit) {
            auto& slot=blocks[producer%slotCount];unsigned free=0;
            if(!slot.state.compare_exchange_strong(free,1,std::memory_order_acquire)) {
                dropped.fetch_add(1,std::memory_order_relaxed);return;
            }
            slot.frames=(std::min)(framesLimit,count-offset);slot.sequence=++nextSequence;
            for(unsigned c=0;c<channels;++c)for(unsigned f=0;f<slot.frames;++f)
                slot.input[c*framesLimit+f]=float(input[c][offset+f]);
            slot.state.store(2,std::memory_order_release);++producer;
        }
    }
    unsigned consume(float* output) noexcept {
        auto& slot=blocks[consumer%slotCount];unsigned ready=2;
        if(!slot.state.compare_exchange_strong(ready,3,std::memory_order_acquire))return 0;
        const auto count=slot.frames;
        if(count && count<=framesLimit && channels<=channelsLimit)
            for(unsigned c=0;c<channels;++c)std::copy_n(slot.input+c*framesLimit,count,output+c*framesLimit);
        slot.state.store(0,std::memory_order_release);++consumer;
        return count<=framesLimit ? count : 0;
    }
};
}
