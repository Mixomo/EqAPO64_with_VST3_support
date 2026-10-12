#include "stdafx.h"
#include "filters/OutProcVSTPluginFilter.h"
#include <tlhelp32.h>
#include <vector>
#include <algorithm>
#include <fstream>
#include <iostream>

static DWORD ownHostPid() {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);DWORD pid=0;
    if(snapshot!=INVALID_HANDLE_VALUE && Process32FirstW(snapshot,&entry))do {
        if(entry.th32ParentProcessID==GetCurrentProcessId() && std::wstring(entry.szExeFile)==L"EqApoOutProcHost.exe") {pid=entry.th32ProcessID;break;}
    }while(Process32NextW(snapshot,&entry));
    if(snapshot!=INVALID_HANDLE_VALUE)CloseHandle(snapshot);return pid;
}
static unsigned long long cpu100ns(HANDLE process) {
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(process,&created,&exited,&kernel,&user))return 0;
    ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return k.QuadPart+u.QuadPart;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=4)return 2;const unsigned seconds=std::stoul(argv[3]);if(!seconds || seconds>55)return 2;
    OutProcVSTPluginFilter filter(argv[1],L"",{{L"#7",.25f}},L"",false);
    filter.initialize(48000,480,{L"L",L"R"});Sleep(500);
    const DWORD pid=ownHostPid();HANDLE host=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
    if(!host)return 3;
    std::vector<double> samples;bool stable=true;double sum=0;
    auto previous=cpu100ns(host);auto previousTime=GetTickCount64();
    for(unsigned i=0;i<seconds*4;++i) {
        Sleep(250);const auto current=cpu100ns(host),currentTime=GetTickCount64();
        const double percentage=100.*double(current-previous)/(10000.*double(currentTime-previousTime));
        samples.push_back(percentage);sum+=percentage;previous=current;previousTime=currentTime;
        stable &= ownHostPid()==pid && WaitForSingleObject(host,0)==WAIT_TIMEOUT;
    }
    CloseHandle(host);std::sort(samples.begin(),samples.end());
    const auto at=[&](double quantile){return samples[size_t((samples.size()-1)*quantile)];};
    const double average=sum/samples.size();const bool passed=stable && average<10.;
    std::ofstream report(argv[2]);report<<"{\"status\":\""<<(passed?"PASS":"FAIL")<<"\",\"pid\":"<<pid<<",\"idle_seconds\":"<<seconds
        <<",\"stable_host\":"<<(stable?"true":"false")<<",\"cpu_one_core_percent\":{\"average\":"<<average<<",\"p95\":"<<at(.95)<<",\"p99\":"<<at(.99)<<",\"max\":"<<samples.back()
        <<"},\"limits\":\"Actual private host idle CPU; Windows CPU accounting granularity; not DSP throughput, GUI CPU or installed audiodg\"}\n";
    std::cout<<(passed?"PASS":"FAIL")<<" idle_cpu_percent="<<average<<" stable="<<stable<<'\n';return passed?0:1;
}
