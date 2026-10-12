#include "stdafx.h"
#include "outproc/BrokerAudioSession.h"
#include "outproc/BrokerCatalog.h"
#include "outproc/AsyncAudioClient.h"
#include <chrono>
#include <thread>
#include <fstream>
#include <cmath>
#include <avrt.h>
#include <mmsystem.h>

static std::wstring uniqueId(){GUID guid{};CoCreateGuid(&guid);wchar_t text[64]{};StringFromGUID2(guid,text,64);std::wstring id(text);return id.substr(1,id.size()-2);}
static bool launch(const std::wstring& host,const std::wstring& id,const std::wstring& space,bool launcher,PROCESS_INFORMATION& child){
    std::wstring command=L"\""+host+L"\" "+(launcher?L"--broker-launch ":L"")+L"--broker-session "+id+L" --broker-space "+space;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);return CreateProcessW(host.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE;
}
int wmain(int argc,wchar_t** argv){
    if(argc==6 && std::wstring(argv[5])==L"--crash-creator"){
        wchar_t exe[32768]{};if(!GetModuleFileNameW(nullptr,exe,32768))return 20;
        const std::wstring marker=std::wstring(argv[3])+L".child";
        std::wstring command=L"\""+std::wstring(exe)+L"\" \""+argv[1]+L"\" \""+argv[2]+L"\" \""+marker+L"\" "+argv[4]+L" --creator-child";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION creator{};
        if(!CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&creator))return 21;
        const auto creatorWait=WaitForSingleObject(creator.hProcess,15000);DWORD creatorExit=99;GetExitCodeProcess(creator.hProcess,&creatorExit);
        if(creatorWait!=WAIT_OBJECT_0){TerminateProcess(creator.hProcess,99);WaitForSingleObject(creator.hProcess,1000);}
        CloseHandle(creator.hThread);CloseHandle(creator.hProcess);
        std::ifstream info(marker);unsigned pid=0;std::uint64_t consumed=0;double ratio=0;info>>pid>>consumed>>ratio;
        HANDLE worker=pid?OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid):nullptr;
        DWORD code=99;DWORD waited=WAIT_FAILED;
        if(worker){waited=WaitForSingleObject(worker,10000);GetExitCodeProcess(worker,&code);if(waited!=WAIT_OBJECT_0){TerminateProcess(worker,99);WaitForSingleObject(worker,1000);}CloseHandle(worker);}
        const bool passed=creatorWait==WAIT_OBJECT_0 && creatorExit==23 && consumed>=550 && std::abs(ratio-3)<.105 && waited==WAIT_OBJECT_0 && code==24;
        std::ofstream report(argv[3]);report<<"{\"status\":\""<<(passed?"PASS":"FAIL_HOST")<<"\",\"creator_exit\":"<<creatorExit<<",\"worker_exit\":"<<code<<",\"consumed_before_crash\":"<<consumed<<",\"gain_ratio\":"<<ratio<<",\"limits\":\"Actual isolated creator process terminated without destructors; same-user IPC, no installed audiodg proof\"}";
        return passed?0:1;
    }
    if(argc!=5 && argc!=6)return 2;const bool expireLease=argc==6 && std::wstring(argv[5])==L"--expire-lease";const std::wstring plugin=argv[1],host=argv[2];const auto param=unsigned(std::stoul(argv[4]));
    OutProcVSTConfig config;config.libraryPath=plugin;config.paramMap[L"#"+std::to_wstring(param)]=.25f;
    EqApoBroker::Owner session;if(!session.create(uniqueId(),EqApoControl::currentUserSid(),config,48000,2,480))return 3;
    EqApoBroker::Catalog catalog;const bool useCatalog=argc==6 && std::wstring(argv[5])==L"--catalog";
    PROCESS_INFORMATION starter{};
    if(useCatalog){
        if(!catalog.open(session.space,EqApoControl::currentUserSid(),true) || !catalog.publish(session))return 30;
        std::wstring command=L"\""+host+L"\" --user-broker --broker-once";STARTUPINFOW startup{};startup.cb=sizeof(startup);
        if(!CreateProcessW(host.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&starter))return 4;
    }else if(!launch(host,session.id,session.space,true,starter))return 4;
    DWORD starterExit=99;const auto starterWait=WaitForSingleObject(starter.hProcess,5000);GetExitCodeProcess(starter.hProcess,&starterExit);
    CloseHandle(starter.hThread);CloseHandle(starter.hProcess);if(starterWait!=WAIT_OBJECT_0 || starterExit!=0)return 5;
    HANDLE worker=nullptr;
    for(unsigned i=0;i<500 && !worker;++i){const auto pid=session.metadata->workerPid.load();if(pid)worker=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!worker)Sleep(10);}
    if(!worker)return 6;
    // A second opener cannot steal the authenticated generation's DSP owner.
    PROCESS_INFORMATION duplicate{};bool duplicateRejected=false;
    if(launch(host,session.id,session.space,false,duplicate)){
        DWORD code=99;const auto waited=WaitForSingleObject(duplicate.hProcess,5000);GetExitCodeProcess(duplicate.hProcess,&code);
        duplicateRejected=waited==WAIT_OBJECT_0 && code==17;if(waited!=WAIT_OBJECT_0){TerminateProcess(duplicate.hProcess,99);WaitForSingleObject(duplicate.hProcess,1000);}
        CloseHandle(duplicate.hThread);CloseHandle(duplicate.hProcess);
    }
    timeBeginPeriod(1);DWORD index=0;HANDLE priority=AvSetMmThreadCharacteristicsW(L"Pro Audio",&index);
    EqApoAsync::AudioClient audio;audio.prepare(2,480,48000);
    double a[480],b[480],x[480],y[480];double* input[]={a,b};double* output[]={x,y};double rms[2]{};bool finite=true,edited=false;
    std::uint64_t position=0;auto deadline=std::chrono::steady_clock::now();
    for(unsigned stage=0;stage<2;++stage){
        if(stage==1){edited=session.header->parameters.publish(session.metadata->generation,param,.75,17);SetEvent(session.request);}
        for(unsigned n=0;n<300;++n){
            for(unsigned f=0;f<480;++f)a[f]=b[f]=.1*std::sin((position+f)*6.283185307179586*997/48000);
            audio.process(output,input,480,session.header);SetEvent(session.request);
            for(unsigned f=0;f<480;++f){finite &= std::isfinite(x[f]) && std::isfinite(y[f]);if(n>=250)rms[stage]+=x[f]*x[f];}
            position+=480;deadline+=std::chrono::milliseconds(10);std::this_thread::sleep_until(deadline);
        }rms[stage]=std::sqrt(rms[stage]/24000);
    }
    if(priority)AvRevertMmThreadCharacteristics(priority);timeEndPeriod(1);
    const bool alive=WaitForSingleObject(worker,0)==WAIT_TIMEOUT;const auto consumed=session.header->consumed.load(),sequence=session.header->parameterSequence.load();
    if(argc==6 && std::wstring(argv[5])==L"--creator-child"){
        if(!finite || !edited || !alive || sequence!=1)return 22;
        {std::ofstream marker(argv[3]);marker<<session.metadata->workerPid.load()<<' '<<consumed<<' '<<rms[1]/rms[0];marker.flush();if(!marker.good())return 22;}
        // Deliberately skip Owner destruction and shutdown publication in this owned child.
        TerminateProcess(GetCurrentProcess(),23);return 23;
    }
    if(expireLease)session.stopHeartbeat();else SetEvent(session.shutdown);
    const auto shutdownWait=WaitForSingleObject(worker,expireLease?10000:5000);DWORD workerExit=99;GetExitCodeProcess(worker,&workerExit);
    if(shutdownWait!=WAIT_OBJECT_0){TerminateProcess(worker,99);WaitForSingleObject(worker,1000);}CloseHandle(worker);
    const double expected=.1/std::sqrt(2.);const bool passed=duplicateRejected && finite && edited && alive && consumed>=550 && sequence==1
        && std::abs(rms[0]/(.25*expected)-1)<.035 && std::abs(rms[1]/(.75*expected)-1)<.035 && shutdownWait==WAIT_OBJECT_0
        && (expireLease?workerExit==24:workerExit==0 && session.metadata->workerPid.load()==0);
    std::ofstream report(argv[3]);report<<"{\"status\":\""<<(passed?"PASS":"FAIL_HOST")<<"\",\"namespace\":\""<<(session.space==L"Global"?"Global":"Local")
        <<"\",\"starter_exited_before_pcm\":true,\"duplicate_worker_rejected\":"<<(duplicateRejected?"true":"false")
        <<",\"worker_alive_after_starter_exit\":"<<(alive?"true":"false")<<",\"before_rms\":"<<rms[0]<<",\"after_rms\":"<<rms[1]
        <<",\"gain_ratio\":"<<rms[1]/rms[0]<<",\"consumed\":"<<consumed<<",\"parameter_sequence\":"<<sequence
        <<",\"worker_exit\":"<<workerExit<<",\"catalog_discovery\":"<<(useCatalog?"true":"false")<<",\"lease_expired_trial\":"<<(expireLease?"true":"false")<<",\"owner_released\":"<<(session.metadata->workerPid.load()==0?"true":"false")
        <<",\"limits\":\"Actual SDK DSP using named secured audio/state IPC after temporary launcher exits; same-user harness, no service-session or installed audiodg proof\"}";
    return passed?0:1;
}
