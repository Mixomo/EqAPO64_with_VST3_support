#include "stdafx.h"
#include "filters/OutProcVSTPluginFilter.h"
#include <chrono>
#include <iostream>
#include <fstream>
#include <vector>

int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;
    SetEnvironmentVariableW(L"EQAPO_FIXTURE_FAULT",L"hang");
    OutProcVSTPluginFilter filter(argv[1],L"",{{L"#7",.25f}},L"",true);
    filter.initialize(48000,960,{L"L",L"R"});
    std::vector<double> a(960,.1),b(960,.1),x(960),y(960);double* input[]={a.data(),b.data()};double* output[]={x.data(),y.data()};
    std::vector<double> times;auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<53;++i) {
        auto before=std::chrono::steady_clock::now();filter.process(output,input,960);
        if(i>=50)times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count());
    }
    const bool passed=filter.getAnalysisStatus()!=0 && times[1]<100 && times[2]<100;
    std::ofstream report(argv[2]);report<<"{\"status\":\""<<(passed?"PASS":"FAIL")<<"\",\"analysis_status\":"<<filter.getAnalysisStatus()<<",\"last_block_ms\":["<<times[0]<<','<<times[1]<<','<<times[2]<<"],\"elapsed_seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<",\"checks\":\"One bounded timeout, persistent explicit failure, subsequent blocks fail fast\"}\n";
    std::cout<<(passed?"PASS":"FAIL")<<" times_ms="<<times[0]<<','<<times[1]<<','<<times[2]<<" status="<<filter.getAnalysisStatus()<<'\n';return passed?0:1;
}
