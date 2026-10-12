#pragma once

#include <cstddef>
#include <atomic>
#include <new>
#include <cstdint>
#include <limits>
#include "OutProcParameterMailbox.h"

constexpr std::uint32_t OUTPROC_AUDIO_MAGIC = 0x4F504147; // OPAG
constexpr std::uint32_t OUTPROC_AUDIO_VERSION = 6;

constexpr std::uint32_t OUTPROC_AUDIO_STATUS_OK = 0;
constexpr std::uint32_t OUTPROC_AUDIO_STATUS_ERROR = 1;

constexpr std::uint32_t OUTPROC_AUDIO_DSP_GAIN = 1;
constexpr std::uint32_t OUTPROC_AUDIO_DSP_BIQUAD = 2;
constexpr std::uint32_t OUTPROC_AUDIO_DSP_VST = 3;
constexpr std::uint32_t OUTPROC_GUI_INFO_MAGIC = 0x4F504749; // OPGI
constexpr std::uint32_t OUTPROC_GUI_INFO_VERSION = 2;

struct OutProcGuiInfo
{
	std::uint32_t magic;
	std::uint32_t version;
	std::uint32_t processId;
	std::uint32_t reserved;
    std::atomic<std::uint64_t> stateGeneration{0};
};

constexpr unsigned OUTPROC_AUDIO_SLOTS=4;
enum : std::uint32_t { SLOT_FREE=0,SLOT_INPUT=1,SLOT_PROCESSING=2,SLOT_OUTPUT=3 };
struct alignas(64) OutProcAudioSlot {
    std::atomic<std::uint32_t> state{SLOT_FREE};
    std::uint32_t frames=0,status=0,reserved=0;
    std::uint64_t sequence=0,streamSample=0,epoch=0;
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,"IPC requires lock-free atomics");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,"IPC requires x64 atomics");
struct alignas(64) OutProcAudioHeader
{
	std::uint32_t magic;
	std::uint32_t version;
	std::uint32_t sampleRate;
	std::uint32_t channelCount;
	std::uint32_t maxFrames;
	std::uint32_t frameCount;
	std::uint32_t smoothingSamples;
	std::uint32_t dspType;
	double gainDb;
	double biquadA0;
	double biquadA1;
	double biquadA2;
	double biquadB1;
	double biquadB2;
	std::uint64_t requestSeq;
	std::uint64_t responseSeq;
	std::uint32_t status;
	std::uint32_t hostState;
    std::atomic<std::uint64_t> heartbeat{0},underruns{0},overruns{0},produced{0},consumed{0};
    std::atomic<std::uint32_t> pluginLatency{0},ready{0},lastError{0};
    std::atomic<std::uint64_t> analysisRequest{0},analysisComplete{0},stateGeneration{0};
    std::uint32_t analysisOnly=0;
    std::uint32_t asyncTransport=1;
    std::atomic<std::uint64_t> sessionEpoch{0};
    std::atomic<std::uint64_t> analysisCpu100ns{0};
    EqApoControl::ParameterMailbox parameters;
    std::atomic<std::uint64_t> parameterSequence{0},parameterRejected{0};
};

inline bool OutProcAudioSampleCount(std::uint32_t channelCount, std::uint32_t frameCount, std::size_t& sampleCount)
{
	if (channelCount == 0 || channelCount>64 || frameCount == 0 || frameCount>65536)
		return false;

	const std::size_t channels = channelCount;
	const std::size_t frames = frameCount;
	if (channels > (std::numeric_limits<std::size_t>::max)() / frames)
		return false;

	sampleCount = channels * frames;
	return true;
}

inline bool OutProcAudioSharedMemorySize(std::uint32_t channelCount, std::uint32_t maxFrames, std::size_t& byteCount)
{
	std::size_t sampleCount = 0;
	if (!OutProcAudioSampleCount(channelCount, maxFrames, sampleCount))
		return false;

	if (sampleCount > ((std::numeric_limits<std::size_t>::max)() - sizeof(OutProcAudioHeader) - OUTPROC_AUDIO_SLOTS*sizeof(OutProcAudioSlot)) / (2 * sizeof(double) * (OUTPROC_AUDIO_SLOTS+1)))
		return false;

	byteCount = sizeof(OutProcAudioHeader) + ((sampleCount * 2 * sizeof(double)+63)&~std::size_t(63)) + OUTPROC_AUDIO_SLOTS*((sizeof(OutProcAudioSlot)+2*sampleCount*sizeof(double)+63)&~std::size_t(63));
	return true;
}

inline double* OutProcAudioInput(OutProcAudioHeader* header)
{
	return reinterpret_cast<double*>(header + 1);
}

inline double* OutProcAudioOutput(OutProcAudioHeader* header)
{
	return OutProcAudioInput(header) + static_cast<std::size_t>(header->channelCount) * header->maxFrames;
}

inline OutProcAudioSlot* OutProcSlot(OutProcAudioHeader* h,unsigned index) {
    const auto samples=static_cast<std::size_t>(h->channelCount)*h->maxFrames;
    auto* base=reinterpret_cast<unsigned char*>(h+1)+((2*samples*sizeof(double)+63)&~std::size_t(63));
    return reinterpret_cast<OutProcAudioSlot*>(base+index*((sizeof(OutProcAudioSlot)+2*samples*sizeof(double)+63)&~std::size_t(63)));
}
inline double* OutProcSlotInput(OutProcAudioSlot* slot) { return reinterpret_cast<double*>(slot+1); }
inline double* OutProcSlotOutput(OutProcAudioHeader* h,OutProcAudioSlot* slot) {
    return OutProcSlotInput(slot)+static_cast<std::size_t>(h->channelCount)*h->maxFrames;
}
inline void OutProcInitializeSlots(OutProcAudioHeader* h) {
    for(unsigned i=0;i<OUTPROC_AUDIO_SLOTS;++i) new(OutProcSlot(h,i)) OutProcAudioSlot();
}
