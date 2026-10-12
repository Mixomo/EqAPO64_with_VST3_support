#include <atomic>
#include <cstdlib>
#include <new>
#include <sstream>
#include <iostream>
#include <limits>
#include "outproc/OutProcVSTConfig.h"
#include "outproc/BrokerStreamScope.h"

static std::atomic<std::size_t> largest{0};
void* operator new(std::size_t size) {
    auto old=largest.load();while(old<size && !largest.compare_exchange_weak(old,size)){}
    if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
int main() {
    EqApoBroker::StreamScope firstStream,secondStream;
    const auto first=firstStream.instanceFor(L"same-editor-module");
    if(first.empty() || first!=firstStream.instanceFor(L"same-editor-module") ||
       first==secondStream.instanceFor(L"same-editor-module") || first==firstStream.instanceFor(L"another-module") ||
       !firstStream.instanceFor(L"").empty())return 9;
    std::cout<<"stream_scope_reload_stability_and_isolation=PASS\n";
    firstStream.beginGeneration();
    const auto replacement=firstStream.instanceFor(L"same-editor-module");
    if(replacement.empty() || replacement==first || replacement!=firstStream.instanceFor(L"same-editor-module"))return 10;
    std::cout<<"explicit_generation_separation=PASS\n";
    firstStream.beginGeneration(); // Bypassed graph: this module is absent.
    firstStream.beginGeneration();
    if(firstStream.instanceFor(L"same-editor-module")!=replacement)return 11;
    firstStream.beginGeneration();
    if(firstStream.instanceFor(L"same-editor-module")==replacement)return 12;
    EqApoBroker::StreamScope changedState;
    const auto initialState=changedState.instanceFor(L"module",L"state-and-layout-A");
    changedState.beginGeneration();changedState.beginGeneration();
    if(changedState.instanceFor(L"module",L"state-and-layout-B")==initialState)return 13;
    std::cout<<"retired_bypass_reuse_and_changed_state_isolation=PASS\n";
    const std::uint32_t claimed=OUTPROC_VST_CONFIG_MAX_STRING_LENGTH;
    std::string shortPayload(reinterpret_cast<const char*>(&claimed),sizeof(claimed));
    std::istringstream truncated(shortPayload,std::ios::binary);
    std::wstring value=L"unchanged";largest=0;
    const bool rejected=!OutProcReadString(truncated,value);
    const auto allocation=largest.load();
    std::cout<<"truncated_rejected="<<rejected<<" largest_allocation="<<allocation<<"\n";
    if(!rejected || allocation>65536)return 1;
    OutProcVSTConfig original;original.libraryPath=L"C:\\plugins\\test.vst3";
    original.classUID=L"0123456789ABCDEF";original.chunkData=std::wstring(10000,L'x');
    original.paramMap={{L"#7",.25f}};
    std::ostringstream encoded(std::ios::binary);
    if(!OutProcWriteVSTConfigStream(encoded,original))return 2;
    std::istringstream input(encoded.str(),std::ios::binary);OutProcVSTConfig restored;
    if(!OutProcReadVSTConfigStream(input,restored) || restored.libraryPath!=original.libraryPath ||
       restored.classUID!=original.classUID || restored.chunkData!=original.chunkData || restored.paramMap!=original.paramMap)return 3;
    const auto bytes=encoded.str();
    for(std::size_t n=0;n<bytes.size();++n){
        std::istringstream partial(bytes.substr(0,n),std::ios::binary);OutProcVSTConfig candidate;
        if(OutProcReadVSTConfigStream(partial,candidate))return 4;
    }
    std::cout<<"roundtrip_and_all_truncation_boundaries=PASS\n";
    OutProcVSTConfig previous=original;
    std::istringstream damaged(bytes.substr(0,bytes.size()-1),std::ios::binary);
    if(OutProcReadVSTConfigStream(damaged,previous) || previous.paramMap!=original.paramMap || previous.chunkData!=original.chunkData)return 5;
    original.paramMap[L"#7"]=std::numeric_limits<float>::quiet_NaN();
    std::ostringstream invalid(std::ios::binary);OutProcWriteVSTConfigStream(invalid,original);
    std::istringstream nonfinite(invalid.str(),std::ios::binary);
    if(OutProcReadVSTConfigStream(nonfinite,previous))return 6;
    for(std::uint32_t version=1;version<=3;++version){
        std::ostringstream legacy(std::ios::binary);
        const auto magic=OUTPROC_VST_CONFIG_MAGIC;const int classIndex=4;const std::uint32_t count=0;
        legacy.write(reinterpret_cast<const char*>(&magic),sizeof(magic));legacy.write(reinterpret_cast<const char*>(&version),sizeof(version));
        OutProcWriteString(legacy,L"legacy.dll");
        if(version>=2)legacy.write(reinterpret_cast<const char*>(&classIndex),sizeof(classIndex));
        if(version>=3)OutProcWriteString(legacy,L"legacy-uid");
        OutProcWriteString(legacy,L"legacy-state");legacy.write(reinterpret_cast<const char*>(&count),sizeof(count));
        std::istringstream reader(legacy.str(),std::ios::binary);OutProcVSTConfig result;
        if(!OutProcReadVSTConfigStream(reader,result) || result.libraryPath!=L"legacy.dll" || result.chunkData!=L"legacy-state" ||
           result.vst3ClassIndex!=(version>=2?4:0) || result.classUID!=(version>=3?L"legacy-uid":L""))return 7;
    }
    std::cout<<"transactional_failure_nonfinite_and_versions_1_2_3=PASS\n";return 0;
}
