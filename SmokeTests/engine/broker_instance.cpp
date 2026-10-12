#include "outproc/BrokerInstanceChannel.h"
#include <iostream>
#include <thread>
#include <set>

int wmain(){
    GUID guid{};CoCreateGuid(&guid);wchar_t text[64]{};StringFromGUID2(guid,text,64);
    const std::wstring host=std::wstring(text).substr(1,36);constexpr std::uint64_t identity=192;
    auto owner=std::make_unique<EqApoBroker::InstanceChannel>();
    if(!owner->create(host,identity))return 2;
    EqApoBroker::InstanceChannel first,second,duplicate,wrong;
    if(!first.open(host,identity) || !second.open(host,identity) || duplicate.create(host,identity) || wrong.open(host,identity+1))return 3;
    const auto generation=first.getGeneration();
    if(first.submit(host,L"Local",1,generation-1) || first.submit(host,L"Everyone",1,generation) || first.submit(host,L"Local",0,generation))return 4;
    std::atomic<bool> failed{false},doneA{false},doneB{false};
    auto produce=[&](EqApoBroker::InstanceChannel& client,unsigned offset,std::atomic<bool>& done){
        for(unsigned n=1;n<=80;++n){const auto start=GetTickCount64();
            while(!client.submit(host,L"Local",offset+n,generation)){if(GetTickCount64()-start>3000){failed=true;done=true;return;}std::this_thread::yield();}}
        done=true;
    };
    std::thread a([&]{produce(first,0,doneA);}),b([&]{produce(second,1000,doneB);});
    std::set<std::uint64_t> sequences,transports;const auto start=GetTickCount64();
    while(sequences.size()<160 && GetTickCount64()-start<5000){EqApoBroker::InstanceRequest request;
        if(owner->take(request)){sequences.insert(request.sequence);transports.insert(request.transportGeneration);owner->acknowledge(request.sequence,true);}
        else if(doneA && doneB)break;else std::this_thread::yield();
    }
    a.join();b.join();if(failed || sequences.size()!=160 || transports.size()!=160)return 5;
    owner->acknowledge(160,true);owner->acknowledge(1,true);if(first.getAdoptedSequence()!=160)return 11;
    owner.reset();if(first.submit(host,L"Local",9000,generation))return 6;
    auto replacement=std::make_unique<EqApoBroker::InstanceChannel>();if(!replacement->create(host,identity))return 7;
    EqApoBroker::InstanceChannel fresh;if(!fresh.open(host,identity) || fresh.getGeneration()==generation)return 8;
    if(first.submit(host,L"Local",9001,generation) || !fresh.submit(host,L"Local",9002,fresh.getGeneration()))return 9;
    EqApoBroker::InstanceRequest request;if(!replacement->take(request) || request.transportGeneration!=9002)return 10;
    std::cout<<"PASS owner_exclusive=1 identity_validated=1 concurrent_unique_requests=160 retired_rejected=1 generation_isolated=1\n";return 0;
}
