#include "stdafx.h"
#include "filters/OutProcVSTPluginFilter.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <new>
#include <thread>

// Fault injection belongs only to this engineering executable. No vendor
// code or production callback is patched; delay the first control allocation.
static thread_local bool slowControlAllocation=false;
static std::atomic<bool> allocationDelayed{false};
static void delayAllocation() {
    if(slowControlAllocation) {
        slowControlAllocation=false;allocationDelayed.store(true);
        Sleep(80);
    }
}
void* operator new(std::size_t n){delayAllocation();if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t alignment){delayAllocation();if(auto p=_aligned_malloc(n?n:1,std::size_t(alignment)))return p;throw std::bad_alloc();}
void operator delete(void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{_aligned_free(p);}

int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;
    OutProcVSTPluginFilter filter(argv[1],L"",{{L"#0",1.f},{L"#3",.65f}},L"",false);
    constexpr unsigned rate=48000,frames=32,warmup=1500,measured=750;
    filter.initialize(rate,frames,{L"L",L"R"});
    std::atomic<bool> begin{false};bool published=false;
    std::thread control([&](){
        while(!begin.load(std::memory_order_acquire))Sleep(1);
        slowControlAllocation=true;
        for(unsigned attempt=0;attempt<100 && !published;++attempt) {
            const auto diagnostic=filter.getDiagnostics();
            if(diagnostic.available)published=filter.publishParameter(diagnostic.parameterGeneration,2,.575188,0);
            if(!published)Sleep(1);
        }
        slowControlAllocation=false;
    });
    double a[frames],b[frames],x[frames],y[frames];double* input[]={a,b};double* output[]={x,y};
    double energy=0;bool finite=true;auto deadline=std::chrono::steady_clock::now();
    for(unsigned block=0;block<warmup+measured;++block) {
        if(block==warmup)begin.store(true,std::memory_order_release);
        for(unsigned f=0;f<frames;++f)a[f]=b[f]=.1*std::sin((block*frames+f)*6.283185307179586*997/rate);
        filter.process(output,input,frames);
        for(unsigned f=0;f<frames;++f) {
            finite &= std::isfinite(x[f]) && std::isfinite(y[f]);
            if(block>=warmup)energy+=x[f]*x[f];
        }
        deadline+=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(double(frames)/rate));
        std::this_thread::sleep_until(deadline);
    }
    control.join();const auto diagnostic=filter.getDiagnostics();
    const double rms=std::sqrt(energy/(measured*frames));
    const double expected=.1/std::sqrt(2.)*std::pow(10.,9./20.);
    const bool passed=allocationDelayed && published && finite && std::abs(rms/expected-1)<.035;
    std::ofstream report(argv[2]);report<<"{\"status\":\""<<(passed?"PASS":"FAIL_HOST")
        <<"\",\"control_allocation_delayed\":"<<(allocationDelayed?"true":"false")
        <<",\"published\":"<<(published?"true":"false")<<",\"rms\":"<<rms<<",\"expected_rms\":"<<expected
        <<",\"processing_quantum\":"<<diagnostic.processingQuantum<<",\"parameter_sequence\":"<<diagnostic.parameterSequence
        <<",\"limits\":\"Actual OOP DSP and public parameter API; synthetic 80ms host control allocation delay only, no installed audiodg proof\"}";
    return passed?0:1;
}
