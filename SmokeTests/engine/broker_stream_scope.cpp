#include "outproc/BrokerStreamScope.h"
#include <iostream>
#include <stdexcept>

static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int main(){
    try {
        EqApoBroker::StreamScope scope;
        scope.beginGeneration();const auto first=scope.instanceFor(L"module",L"format");scope.endGeneration();
        check(!first.empty(),"initial identity");
        scope.beginGeneration();const auto active=scope.instanceFor(L"module",L"format");scope.endGeneration();
        check(active!=first,"adjacent active generations must remain independent");
        for(unsigned edit=0;edit<12;++edit){scope.beginGeneration();scope.markBypassed(L"module");scope.endGeneration();}
        scope.beginGeneration();const auto retained=scope.instanceFor(L"module",L"format");scope.endGeneration();
        check(retained==active,"configuration churn during bypass must preserve worker identity");
        scope.beginGeneration();const auto next=scope.instanceFor(L"module",L"format");
        scope.markBypassed(L"module");scope.endGeneration();
        check(next!=retained,"commented duplicate must not make an active generation reusable");
        scope.beginGeneration();const auto independent=scope.instanceFor(L"module",L"format");scope.endGeneration();
        check(independent!=next,"active identity survives duplicate-row marking");
        scope.beginGeneration();scope.markBypassed(L"module");scope.endGeneration();
        scope.beginGeneration();const auto changed=scope.instanceFor(L"module",L"different-format");scope.endGeneration();
        check(changed!=independent,"incompatible format must not reuse retired worker");
        scope.beginGeneration();scope.endGeneration();
        scope.beginGeneration();const auto recreated=scope.instanceFor(L"module",L"different-format");scope.endGeneration();
        check(recreated!=changed,"removed modules must release retained identity");
        std::cout<<"PASS bypass churn / adjacent active isolation / incompatible format / removal\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
