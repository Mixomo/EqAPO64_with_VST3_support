#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <memory>
#include <vector>
#include <cstring>
#include "pluginterfaces/vst/ivstparameterchanges.h"

namespace EqApoRT {
template<class T, std::size_t N> class SpscRing {
public:
    bool push(const T& value) noexcept {
        auto w=write.load(std::memory_order_relaxed), next=(w+1)%N;
        if(next==read.load(std::memory_order_acquire)) return false;
        values[w]=value; write.store(next,std::memory_order_release); return true;
    }
    bool pop(T& value) noexcept {
        auto r=read.load(std::memory_order_relaxed);
        if(r==write.load(std::memory_order_acquire)) return false;
        value=values[r]; read.store((r+1)%N,std::memory_order_release); return true;
    }
    bool empty() const noexcept {return read.load(std::memory_order_relaxed)==write.load(std::memory_order_acquire);}
private:
    std::array<T,N> values{};
    alignas(64) std::atomic<std::size_t> write{0};
    alignas(64) std::atomic<std::size_t> read{0};
};
struct ParameterEvent { Steinberg::Vst::ParamID id; double value; int offset; };

// Registered on the control thread. When the event ring fills, preserve the
// newest normalized value for every declared ParamID with lock-free stores.
class LatestParameterValues {
    struct Entry {std::atomic<std::uint64_t> bits{0};std::atomic<bool> dirty{false};};
    std::vector<Steinberg::Vst::ParamID> ids;
    std::unique_ptr<Entry[]> entries;
    std::size_t cursor=0;
public:
    void prepare(std::vector<Steinberg::Vst::ParamID> registered) {
        std::sort(registered.begin(),registered.end());
        registered.erase(std::unique(registered.begin(),registered.end()),registered.end());
        ids=std::move(registered);entries=std::make_unique<Entry[]>(ids.size());cursor=0;
    }
    bool hasDirty() const noexcept {
        for(std::size_t i=0;i<ids.size();++i)
            if(entries[i].dirty.load(std::memory_order_acquire))return true;
        return false;
    }
    void remember(Steinberg::Vst::ParamID id,double value,bool overflow) noexcept {
        auto it=std::lower_bound(ids.begin(),ids.end(),id);
        if(it==ids.end()||*it!=id)return;
        auto& entry=entries[std::size_t(it-ids.begin())];std::uint64_t bits;
        std::memcpy(&bits,&value,sizeof(bits));entry.bits.store(bits,std::memory_order_release);
        if(overflow)entry.dirty.store(true,std::memory_order_release);
    }
    void markOverflow(Steinberg::Vst::ParamID id) noexcept {
        auto it=std::lower_bound(ids.begin(),ids.end(),id);
        if(it!=ids.end() && *it==id) entries[std::size_t(it-ids.begin())].dirty.store(true,std::memory_order_release);
    }
    template<class Consumer> void drain(unsigned budget,Consumer consume) noexcept {
        for(unsigned visited=0;visited<budget && visited<ids.size();++visited) {
            if(cursor>=ids.size())cursor=0;auto index=cursor++;
            auto& entry=entries[index];
            if(!entry.dirty.exchange(false,std::memory_order_acq_rel))continue;
            auto bits=entry.bits.load(std::memory_order_acquire);double value;
            std::memcpy(&value,&bits,sizeof(value));
            if(!consume(ids[index],value))entry.dirty.store(true,std::memory_order_release);
        }
    }
};

// Storage belongs to one processor thread. No SDK vector growth or refcount
// deletion is permitted while processing. Overflow is reported to the caller.
class ParameterQueue final : public Steinberg::Vst::IParamValueQueue {
public:
    Steinberg::Vst::ParamID id=0;
    int count=0;
    std::array<std::pair<int,double>,64> points{};
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,void** obj) override {
        if(!obj) return Steinberg::kInvalidArgument;
        if(Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid) || Steinberg::FUnknownPrivate::iidEqual(iid, IParamValueQueue::iid)) {
            *obj=static_cast<IParamValueQueue*>(this); return Steinberg::kResultOk;
        } *obj=nullptr; return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return count; }
    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index,Steinberg::int32& offset,double& value) override {
        if(index<0 || index>=count) return Steinberg::kInvalidArgument;
        offset=points[index].first; value=points[index].second; return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 offset,double value,Steinberg::int32& index) override {
        if(!std::isfinite(value)) return Steinberg::kResultFalse;
        if(count>=int(points.size())) { index=count-1; points[index]={offset,value}; return Steinberg::kResultOk; }
        index=count; points[count++]={offset,value}; return Steinberg::kResultOk;
    }
};
class ParameterChanges final : public Steinberg::Vst::IParameterChanges {
public:
    void clear() noexcept { count=0; }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,void** obj) override {
        if(!obj) return Steinberg::kInvalidArgument;
        if(Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid) || Steinberg::FUnknownPrivate::iidEqual(iid, IParameterChanges::iid)) {
            *obj=static_cast<IParameterChanges*>(this); return Steinberg::kResultOk;
        } *obj=nullptr; return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getParameterCount() override { return count; }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override {
        return index>=0 && index<count ? &queues[index] : nullptr;
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id,Steinberg::int32& index) override {
        for(int i=0;i<count;++i) if(queues[i].id==id) { index=i; return &queues[i]; }
        if(count>=int(queues.size())) { index=-1; return nullptr; }
        index=count++; queues[index].id=id; queues[index].count=0; return &queues[index];
    }
private:
    int count=0;
    std::array<ParameterQueue,256> queues{};
};
}
