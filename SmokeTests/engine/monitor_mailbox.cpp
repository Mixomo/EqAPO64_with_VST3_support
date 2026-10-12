#include "outproc/OutProcControlEndpoint.h"
#include <memory>
#include <iostream>
#include <thread>
#include <vector>

int main(){
    auto box=std::make_unique<EqApoControl::MonitorMailbox>(9);
    std::vector<double> a(480,.25),b(480,-.5);double* input[]={a.data(),b.data()};
    std::vector<float> samples(64*1024);
    box->publish(input,480);if(box->consume(samples.data()))return 1;
    box->enabled.store(GetCurrentProcessId());
    for(unsigned i=0;i<8;++i)box->publish(input,480);
    box->publish(input,480);if(box->dropped.load()!=1)return 2;
    for(unsigned i=0;i<8;++i){if(box->consume(samples.data())!=480)return 3;
        for(unsigned f=0;f<480;++f)if(samples[f]!=.25f || samples[1024+f]!=-.5f)return 4;}
    if(box->consume(samples.data()))return 5;
    std::atomic<bool> done{false};unsigned blocks=0;
    std::thread producer([&](){for(unsigned i=0;i<10000;++i)box->publish(input,480);done.store(true);});
    while(!done.load()){if(box->consume(samples.data()))++blocks;}
    producer.join();while(box->consume(samples.data()))++blocks;
    if(!blocks)return 6;
    const auto session=L"MonitorTest_"+std::to_wstring(GetCurrentProcessId());
    EqApoControl::ControlEndpointOwner owner;
    if(!owner.create(session,EqApoControl::currentUserSid(),17,96000,2))return 7;
    EqApoControl::MonitorReader reader;
    if(!reader.connect(session,17) || reader.sampleRate()!=96000 || reader.channels()!=2)return 8;
    EqApoControl::MonitorReader wrong;if(wrong.connect(session,18))return 9;
    owner.publishMonitorInput(input,480);
    if(reader.consume(samples.data())!=480 || samples[0]!=.25f || samples[1024]!=-.5f)return 10;
    EqApoControl::ControlEndpointOwner replacement;
    if(!replacement.create(session,EqApoControl::currentUserSid(),17,96000,2) || !reader.refresh())return 11;
    replacement.publishMonitorInput(input,480);
    if(reader.consume(samples.data())!=480)return 12;
    std::cout<<"PASS bounded_spsc_blocks="<<blocks<<" secured_input=true generation_refresh=true overflow_nonblocking=true\n";
}
