#include "outproc/BrokerAudioSession.h"
#include "outproc/AsyncAudioClient.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

int wmain(int argc,wchar_t** argv) {
    if(argc!=4)return 2;
    const unsigned delay=std::stoul(argv[3]);
    if(delay!=6500 && delay!=11000)return 2;
    SetEnvironmentVariableW(L"EQAPO_FIXTURE_INITIALIZE_DELAY_MS",argv[3]);
    GUID guid{};CoCreateGuid(&guid);wchar_t text[40]{};StringFromGUID2(guid,text,40);
    OutProcVSTConfig config;config.libraryPath=argv[1];config.paramMap[L"#7"]=.25f;
    EqApoBroker::Owner session;
    if(!session.create(std::wstring(text+1,36),EqApoControl::currentUserSid(),config,48000,2,480))return 3;
    std::wstring command=L"\""+std::wstring(argv[2])+L"\" --broker-session "+session.id+L" --broker-space "+session.space;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    if(!CreateProcessW(argv[2],command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))return 4;
    CloseHandle(child.hThread);
    EqApoAsync::AudioClient audio;audio.prepare(2,480,48000);
    double input[480],left[480],right[480];for(auto& v:input)v=.1;
    double* in[]={input,input};double* out[]={left,right};
    std::uint64_t heartbeat=0;unsigned stalled=0;bool falseStall=false,finite=true,prematureReady=false;
    double finalSample=0;long long firstReady=-1;
    const auto began=std::chrono::steady_clock::now();auto deadline=began;
    for(unsigned block=0;block<1250;++block) {
        audio.process(out,in,480,session.header);SetEvent(session.request);
        for(auto v:left)finite &= std::isfinite(v);
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count();
        const bool ready=session.header->ready.load(std::memory_order_acquire)!=0;
        if(ready && firstReady<0)firstReady=elapsed;
        prematureReady |= ready && elapsed<delay;
        if(block%5==0) {
            const auto now=session.header->heartbeat.load(std::memory_order_acquire);
            bool pending=false;for(unsigned slot=0;slot<OUTPROC_AUDIO_SLOTS;++slot) {
                const auto state=OutProcSlot(session.header,slot)->state.load(std::memory_order_acquire);
                pending |= state==SLOT_INPUT || state==SLOT_PROCESSING;
            }
            stalled=pending && now==heartbeat ? stalled+1 : 0;heartbeat=now;
            falseStall |= stalled>=100 && elapsed<delay;
        }
        finalSample=left[479];
        if(WaitForSingleObject(child.hProcess,0)==WAIT_OBJECT_0)break;
        if(firstReady>=0 && elapsed>=firstReady+1000)break;
        deadline+=std::chrono::milliseconds(10);std::this_thread::sleep_until(deadline);
    }
    DWORD exit=STILL_ACTIVE;GetExitCodeProcess(child.hProcess,&exit);
    const bool success=delay==6500 ? firstReady>=6500 && firstReady<8500 && std::abs(finalSample-.025)<1e-5 && exit==STILL_ACTIVE
        : firstReady<0 && exit==27;
    SetEvent(session.shutdown);
    if(WaitForSingleObject(child.hProcess,4000)!=WAIT_OBJECT_0){TerminateProcess(child.hProcess,99);WaitForSingleObject(child.hProcess,1000);}
    CloseHandle(child.hProcess);
    std::cout<<"{\"delay_ms\":"<<delay<<",\"first_ready_ms\":"<<firstReady<<",\"false_processing_stall\":"<<(falseStall?"true":"false")
        <<",\"premature_audio_readiness\":"<<(prematureReady?"true":"false")<<",\"finite\":"<<(finite?"true":"false")
        <<",\"sample\":"<<finalSample<<",\"worker_exit\":"<<exit<<"}\n";
    return success && !falseStall && !prematureReady && finite ? 0 : 1;
}
