#include "outproc/AsyncAudioClient.h"
#include "helpers/GraphAlignment.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
#include <stdexcept>

static std::atomic<bool> watch{false};
static std::atomic<unsigned long long> allocations{0};
void* operator new(std::size_t n) { if(watch) ++allocations; if(auto p=std::malloc(n)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
static void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }

static void testTimeline(unsigned channels,unsigned rate,unsigned quantum,unsigned pluginLatency) {
    std::size_t bytes=0; check(OutProcAudioSharedMemorySize(channels,quantum,bytes),"mapping size");
    auto* memory=static_cast<unsigned char*>(_aligned_malloc(bytes,64));
    std::memset(memory,0,bytes);
    auto* shared=new(memory) OutProcAudioHeader();
    shared->magic=OUTPROC_AUDIO_MAGIC;shared->version=OUTPROC_AUDIO_VERSION;
    shared->channelCount=channels;shared->maxFrames=quantum;shared->pluginLatency=pluginLatency;
    shared->ready=1;
    OutProcInitializeSlots(shared);
    for(unsigned i=0;i<4;++i) check(reinterpret_cast<std::uintptr_t>(OutProcSlot(shared,i))%64==0,"slot alignment");
    EqApoAsync::AudioClient client;client.prepare(channels,quantum,rate);
    std::vector<std::vector<double>> in(channels,std::vector<double>(quantum)),out=in;
    std::vector<double*> ip(channels),op(channels);
    for(unsigned c=0;c<channels;++c) {ip[c]=in[c].data();op[c]=out[c].data();}
    std::vector<std::vector<double>> hostHistory(channels,std::vector<double>(pluginLatency+1,0));
    std::uint64_t hostPosition=0,position=0;unsigned callbacks=0;
    unsigned blocks[]={32,64,128,256,480,512,960,17,1};
    const auto initialAllocations=allocations.load();
    watch=true;
    for(unsigned cycle=0;cycle<800;++cycle) {
        const unsigned frames=std::min(quantum,blocks[cycle%9]);
        for(unsigned c=0;c<channels;++c) for(unsigned f=0;f<frames;++f) in[c][f]=0.25*std::sin((position+f)*0.003+c*0.1);
        // Missing/dead host for 60 callbacks, then recovery without callback allocations.
        const bool alive=cycle<500 || cycle>=560;
        client.process(op.data(),ip.data(),frames,shared);
        if(cycle==0)check(!client.readyForTransition(),"empty pipeline must not start graph crossfade");
        if(cycle>100 && cycle<490)check(client.readyForTransition(),"valid steady wet must allow graph crossfade");
        if(cycle==550)check(!client.readyForTransition(),"lost host must not masquerade as warmed wet");
        for(unsigned c=0;c<channels;++c) for(unsigned f=0;f<frames;++f) {
            check(std::isfinite(out[c][f]),"NaN/Inf");
            const auto at=position+f;
            if(cycle>100 && cycle<490) {
                const auto delayed=at-(2*quantum+pluginLatency);
                const double expected=0.125*std::sin(delayed*0.003+c*0.1);
                check(std::abs(out[c][f]-expected)<1e-10,"sample timeline / latency alignment");
            }
        }
        position+=frames;
        if(alive) for(unsigned n=0;n<4;++n) {
            OutProcAudioSlot* slot=nullptr;
            for(unsigned i=0;i<4;++i) { auto* q=OutProcSlot(shared,i); if(q->state.load()==SLOT_INPUT && (!slot || q->sequence<slot->sequence)) slot=q; }
            if(!slot) break;
            slot->state.store(SLOT_PROCESSING);
            for(unsigned f=0;f<quantum;++f,++hostPosition) for(unsigned c=0;c<channels;++c) {
                const double value=OutProcSlotInput(slot)[std::size_t(c)*quantum+f];
                auto& history=hostHistory[c]; history[hostPosition%history.size()]=value;
                OutProcSlotOutput(shared,slot)[std::size_t(c)*quantum+f]=hostPosition>=pluginLatency ? 0.5*history[(hostPosition-pluginLatency)%history.size()] : 0;
            }
            slot->status=OUTPROC_AUDIO_STATUS_OK;slot->state.store(SLOT_OUTPUT);
        }
        ++callbacks;
    }
    shared->lastError.store(4);
    for(unsigned n=0;n<rate/100/quantum+2;++n)client.process(op.data(),ip.data(),quantum,shared);
    check(client.readyForTransition(),"quarantined aligned dry must allow bounded graph completion");
    watch=false;
    check(allocations==initialAllocations,"steady-state heap allocation");
    check(shared->overruns>0,"dead host saturation must be counted");
    _aligned_free(memory);
    std::cout<<"PASS timeline channels="<<channels<<" rate="<<rate<<" quantum="<<quantum<<" plugin="<<pluginLatency<<" callbacks="<<callbacks<<" allocations=0\n";
}
static void testGraphAlignment() {
    GraphAlignment first,second;first.prepare(2,48000,480,48000);second.prepare(2,48000,480,48000);
    double a[2][480]{},b[2][480]{};double* ap[]={a[0],a[1]};double* bp[]={b[0],b[1]};
    unsigned latencyA[]={0,0},latencyB[]={257,257};
    unsigned long long position=0;
    const auto before=allocations.load();watch=true;
    for(unsigned block=0;block<80;++block) {
        for(unsigned f=0;f<480;++f)for(unsigned c=0;c<2;++c) {
            a[c][f]=std::sin((position+f)*0.003+c);
            b[c][f]=position+f>=257?std::sin((position+f-257)*0.003+c):0;
        }
        first.capture(ap,480);second.capture(bp,480);
        check(first.setTarget(257,latencyA) && second.setTarget(257,latencyB),"graph latency bound");
        first.render(ap);second.render(bp);
        if(block>2)for(unsigned c=0;c<2;++c)for(unsigned f=0;f<480;++f)
            check(std::abs(a[c][f]-b[c][f])<1e-12,"graphs must align before crossfade");
        position+=480;
    }
    watch=false;check(before==allocations,"graph alignment allocated in callback");
    std::cout<<"PASS graph alignment latency=257 allocations=0\n";
}
int main() {
    try {
        testGraphAlignment();
        for(unsigned rate:{44100u,48000u,96000u}) for(unsigned channels:{1u,2u,6u,8u})
            for(unsigned quantum:{32u,64u,128u,256u,480u,512u,960u}) for(unsigned latency:{0u,257u}) testTimeline(channels,rate,quantum,latency);
        std::cout<<"PASS all realtime timeline contracts\n"; return 0;
    } catch(const std::exception& e) {watch=false;std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
