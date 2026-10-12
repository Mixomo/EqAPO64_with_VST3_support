#pragma once

#include <cstdint>
#include <atomic>
#include <new>

template<class T> struct MeterValue {
    std::atomic<T> value{0};
    MeterValue()=default;
    MeterValue(const MeterValue& other) noexcept : value(other.value.load(std::memory_order_relaxed)) {}
    MeterValue& operator=(const MeterValue& other) noexcept { value.store(other.value.load(std::memory_order_relaxed),std::memory_order_relaxed); return *this; }
    MeterValue& operator=(T v) noexcept { value.store(v,std::memory_order_relaxed); return *this; }
    operator T() const noexcept { return value.load(std::memory_order_relaxed); }
    T operator++(int) noexcept { return value.fetch_add(1,std::memory_order_acq_rel); }
};
static_assert(std::atomic<double>::is_always_lock_free,"Meter requires x64 lock-free doubles");

static const std::uint32_t VUMETER_MAGIC = 0x4F505556u; // VUPO
static const std::uint32_t VUMETER_VERSION = 4;
static const unsigned VUMETER_MAX_CHANNELS = 16;

struct VUMeterSharedData
{
	MeterValue<std::uint32_t> magic;
	MeterValue<std::uint32_t> version;
	MeterValue<std::uint32_t> channelCount;
	MeterValue<std::uint32_t> sampleRate;
	MeterValue<std::uint64_t> sequence;
    std::atomic<std::uint64_t> writerOwner{0};
    std::atomic<std::uint32_t> writerGate{0};
	MeterValue<double> peak[VUMETER_MAX_CHANNELS];
	MeterValue<double> peakHold[VUMETER_MAX_CHANNELS];
	MeterValue<double> rms[VUMETER_MAX_CHANNELS];
	MeterValue<double> lufsMomentary;
	MeterValue<double> lufsShortTerm;
	MeterValue<double> lufsIntegrated;
	MeterValue<double> channelLufsMomentary[VUMETER_MAX_CHANNELS];
	MeterValue<double> channelLufsShortTerm[VUMETER_MAX_CHANNELS];
	MeterValue<double> channelLufsIntegrated[VUMETER_MAX_CHANNELS];
	MeterValue<std::uint32_t> clip[VUMETER_MAX_CHANNELS];
	MeterValue<std::uint32_t> resetRequest;
};
struct VUMeterSnapshot
{
	std::uint32_t magic;
	std::uint32_t version;
	std::uint32_t channelCount;
	std::uint32_t sampleRate;
	std::uint64_t sequence;
	double peak[VUMETER_MAX_CHANNELS];
	double peakHold[VUMETER_MAX_CHANNELS];
	double rms[VUMETER_MAX_CHANNELS];
	double lufsMomentary;
	double lufsShortTerm;
	double lufsIntegrated;
	double channelLufsMomentary[VUMETER_MAX_CHANNELS];
	double channelLufsShortTerm[VUMETER_MAX_CHANNELS];
	double channelLufsIntegrated[VUMETER_MAX_CHANNELS];
	std::uint32_t clip[VUMETER_MAX_CHANNELS];
	std::uint32_t resetRequest;
};

inline void CopyMeterSnapshot(const VUMeterSharedData* s,VUMeterSnapshot& d) noexcept {
 d.magic=s->magic;d.version=s->version;d.channelCount=s->channelCount;d.sampleRate=s->sampleRate;d.sequence=s->sequence;
 d.lufsMomentary=s->lufsMomentary;d.lufsShortTerm=s->lufsShortTerm;d.lufsIntegrated=s->lufsIntegrated;d.resetRequest=s->resetRequest;
 for(unsigned i=0;i<VUMETER_MAX_CHANNELS;++i) {
  d.peak[i]=s->peak[i];d.peakHold[i]=s->peakHold[i];d.rms[i]=s->rms[i];d.clip[i]=s->clip[i];
  d.channelLufsMomentary[i]=s->channelLufsMomentary[i];d.channelLufsShortTerm[i]=s->channelLufsShortTerm[i];d.channelLufsIntegrated[i]=s->channelLufsIntegrated[i];
 }
}
