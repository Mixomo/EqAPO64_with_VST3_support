#pragma once
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>

namespace EqApoControl {

struct ParameterEvent {
    std::uint64_t generation=0,sequence=0;
    std::uint32_t id=0;
    std::int32_t sampleOffset=0;
    double normalized=0;
};

// Shared-memory MPSC mailbox: GUI and Editor may both publish, but a single
// DSP owner consumes this generation. No pointer, allocator, lock or wait.
// Construction happens before publication; generation is immutable afterwards.
class alignas(64) ParameterMailbox {
public:
    static constexpr std::uint64_t capacity=256;
    explicit ParameterMailbox(std::uint64_t generation=0) noexcept:generation(generation) {
        for(std::uint64_t i=0;i<capacity;++i)cells[i].turn.store(i,std::memory_order_relaxed);
    }
    bool publish(std::uint64_t targetGeneration,std::uint32_t id,double normalized,std::int32_t offset) noexcept {
        if(targetGeneration!=generation) {wrongGeneration.fetch_add(1,std::memory_order_relaxed);return false;}
        if(!std::isfinite(normalized) || normalized<0 || normalized>1) {
            invalidValues.fetch_add(1,std::memory_order_relaxed);return false;
        }
        auto position=write.load(std::memory_order_relaxed);
        // Bounded contention: rejected edits are visible, never create a hole.
        for(unsigned attempt=0;attempt<4;++attempt) {
            auto& slot=cells[position%capacity];
            const auto turn=slot.turn.load(std::memory_order_acquire);
            if(turn==position) {
                if(write.compare_exchange_weak(position,position+1,std::memory_order_relaxed)) {
                    slot.event={generation,position+1,id,offset,normalized};
                    slot.turn.store(position+1,std::memory_order_release);
                    accepted.fetch_add(1,std::memory_order_relaxed);return true;
                }
            } else if(turn<position)break;
            else position=write.load(std::memory_order_relaxed);
        }
        dropped.fetch_add(1,std::memory_order_relaxed);return false;
    }
    bool consume(std::uint64_t ownerGeneration,ParameterEvent& event) noexcept {
        // A stale owner cannot consume a newer generation's event.
        if(ownerGeneration!=generation)return false;
        const auto position=read.load(std::memory_order_relaxed);
        auto& slot=cells[position%capacity];
        if(slot.turn.load(std::memory_order_acquire)!=position+1)return false;
        event=slot.event;
        slot.turn.store(position+capacity,std::memory_order_release);
        read.store(position+1,std::memory_order_relaxed);return true;
    }
    const std::uint64_t generation;
    std::atomic<std::uint64_t> accepted{0},dropped{0},wrongGeneration{0},invalidValues{0};
private:
    struct alignas(64) Slot {std::atomic<std::uint64_t> turn{0};ParameterEvent event;};
    alignas(64) std::atomic<std::uint64_t> write{0};
    alignas(64) std::atomic<std::uint64_t> read{0};
    std::array<Slot,capacity> cells;
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,"Parameter IPC requires lock-free x64 atomics");
static_assert((ParameterMailbox::capacity&(ParameterMailbox::capacity-1))==0,"Mailbox capacity is a power of two");

}
