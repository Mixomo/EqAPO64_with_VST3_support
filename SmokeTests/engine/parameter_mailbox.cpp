#include "outproc/OutProcParameterMailbox.h"
#include <thread>
#include <iostream>
#include <limits>
#include <stdexcept>

static void check(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
int main() {
    using namespace EqApoControl;
    try {
        ParameterMailbox mailbox(42);ParameterEvent event;
        check(!mailbox.publish(41,7,.5,17),"stale producer generation rejected");
        check(!mailbox.publish(42,7,std::numeric_limits<double>::quiet_NaN(),0),"NaN rejected");
        check(!mailbox.publish(42,7,1.01,0),"normalized range rejected");
        for(unsigned i=0;i<256;++i)check(mailbox.publish(42,i,i/256.,int(i)),"fill capacity");
        check(!mailbox.publish(42,999,.5,0),"overflow rejected without hole");
        check(!mailbox.consume(41,event),"old DSP cannot steal new event");
        for(unsigned i=0;i<256;++i) {
            check(mailbox.consume(42,event),"drain capacity");
            check(event.generation==42 && event.sequence==i+1 && event.id==i && event.sampleOffset==int(i) && event.normalized==i/256.,"event identity/value/offset/sequence preserved");
        }
        check(!mailbox.consume(42,event),"empty is immediate");
        constexpr unsigned perProducer=50000;
        std::atomic<unsigned> done{0};std::atomic<std::uint64_t> successful{0};
        auto producer=[&](unsigned id) {
            for(unsigned i=0;i<perProducer;++i) {
                if(mailbox.publish(42,id,id==7?.25:.75,int(i)))successful.fetch_add(1,std::memory_order_relaxed);
                // Test pacing only; production publish never waits/retries indefinitely.
                if(i%32==0)std::this_thread::yield();
            }
            done.fetch_add(1,std::memory_order_release);
        };
        std::thread a(producer,7),b(producer,19);std::uint64_t consumed=0,last=256;
        while(done.load(std::memory_order_acquire)!=2 || consumed<successful.load(std::memory_order_relaxed)) {
            if(!mailbox.consume(42,event)) {std::this_thread::yield();continue;}
            check(event.sequence==last+1 && event.generation==42,"concurrent order/generation");last=event.sequence;
            check((event.id==7 && event.normalized==.25) || (event.id==19 && event.normalized==.75),"no torn multi-producer event");++consumed;
        }
        a.join();b.join();check(consumed==successful,"all accepted events delivered");
        check(mailbox.accepted==consumed+256 && mailbox.wrongGeneration==1 && mailbox.invalidValues==2,"telemetry exact");
        std::cout<<"PASS bounded parameter MPSC generation/identity/offset/order/overflow accepted="<<consumed<<" dropped="<<mailbox.dropped<<'\n';
        return 0;
    }catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
