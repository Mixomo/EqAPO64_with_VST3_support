#include "stdafx.h"
#include "FilterEngine.h"
#include <fstream>
#include <chrono>
#include <thread>
#include <cmath>
#include <avrt.h>
#include <mmsystem.h>
int wmain(int argc,wchar_t** argv){
    if(argc!=5)return 2;
    const std::wstring path=argv[2],mode=argv[4];if(mode!=L"INPROC" && mode!=L"OUTPROC")return 2;
    auto write=[&](bool bypass){std::wofstream file(path);if(bypass)file<<L"# ";file<<(mode==L"INPROC"?L"VSTPlugin: ":L"OutProcVSTPlugin: ")<<L"Library \""<<argv[1]<<L"\" #0 1 #1 1 #3 0.3\n";return file.good();};
    if(!write(false))return 3;FilterEngine engine;engine.initialize(48000,2,2,2,3,480,path,true);
    timeBeginPeriod(1);DWORD task=0;HANDLE priority=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task);
    struct Timing{HANDLE priority;~Timing(){if(priority)AvRevertMmThreadCharacteristics(priority);timeEndPeriod(1);}} timing{priority};
    double input[480],left[480],right[480],levels[3]{};double* in[]={input,input};double* out[]={left,right};bool finite=true;unsigned zeroBlocks=0;std::uint64_t position=0;
    for(unsigned phase=0;phase<3;++phase){
        if(phase){if(!write(phase==1))return 3;engine.loadConfig(path);}
        auto deadline=std::chrono::steady_clock::now();
        for(unsigned block=0;block<220;++block){
            for(unsigned f=0;f<480;++f)input[f]=.1*std::sin((position+f)*6.283185307179586*997/48000.);
            engine.process(out,in,480);position+=480;bool silent=true;
            for(auto sample:left){finite&=std::isfinite(sample);silent&=sample==0;if(block>=180)levels[phase]+=sample*sample/19200.;}
            if(block>=180)zeroBlocks+=silent;
            deadline+=std::chrono::milliseconds(10);std::this_thread::sleep_until(deadline);
        }
        levels[phase]=std::sqrt(levels[phase]);
    }
    const auto dry=.1/std::sqrt(2.),wet=dry*std::pow(10.,-.6);
    const bool passed=finite && !zeroBlocks && std::abs(levels[0]/wet-1)<.05 && std::abs(levels[1]/dry-1)<.05 && std::abs(levels[2]/wet-1)<.05;
    std::ofstream report(argv[3]);report<<"{\"status\":\""<<(passed?"PASS":"FAIL")<<"\",\"mode\":\""<<(mode==L"INPROC"?"INPROC":"OUTPROC")<<"\",\"active_rms\":"<<levels[0]<<",\"bypass_rms\":"<<levels[1]<<",\"restored_rms\":"<<levels[2]<<",\"expected_wet_rms\":"<<wet<<",\"expected_dry_rms\":"<<dry<<",\"zero_blocks_in_settled_regions\":"<<zeroBlocks<<",\"finite\":"<<(finite?"true":"false")<<",\"limits\":\"Actual private FilterEngine PCM and module disable/reenable; no GUI required for audio; no installed audiodg certification\"}";
    return passed?0:1;
}
