#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

// One audio owner. A graph retains its raw output history independently of
// other graphs so a transition can align both paths before mixing them.
class GraphAlignment {
    std::vector<double> history;
    std::vector<unsigned> oldDelay,newDelay;
    unsigned channels=0,capacity=0,fadeFrames=1,blockFrames=0;
    std::uint64_t position=0;
    unsigned ramp=0;
public:
    void prepare(unsigned count,unsigned maximumDelay,unsigned maxFrames,unsigned rate) {
        channels=count;capacity=maximumDelay+maxFrames+1;
        history.assign(std::size_t(count)*capacity,0);
        oldDelay.assign(count,0);newDelay.assign(count,0);
        fadeFrames=(std::max)(1u,rate/100);ramp=fadeFrames;position=0;
    }
    void capture(double** raw,unsigned frames) noexcept {
        blockFrames=frames;
        for(unsigned c=0;c<channels;++c) for(unsigned f=0;f<frames;++f)
            history[std::size_t(c)*capacity+(position+f)%capacity]=raw[c][f];
        position+=frames;
    }
    bool setTarget(unsigned target,const unsigned* nativeLatencies) noexcept {
        bool changed=false;
        for(unsigned c=0;c<channels;++c) {
            const unsigned delay=target>=nativeLatencies[c] ? target-nativeLatencies[c] : 0;
            if(delay+blockFrames>=capacity) return false;
            changed|=delay!=newDelay[c];
        }
        if(changed) {
            for(unsigned c=0;c<channels;++c) {
                oldDelay[c]=newDelay[c];newDelay[c]=target-nativeLatencies[c];
            }
            ramp=0;
        }
        return true;
    }
    void render(double** output) noexcept {
        const auto start=position-blockFrames;
        for(unsigned f=0;f<blockFrames;++f) {
            const double blend=(std::min)(1.0,double(ramp+f)/fadeFrames);
            const auto sample=start+f;
            for(unsigned c=0;c<channels;++c) {
                const auto base=std::size_t(c)*capacity;
                const double oldValue=sample>=oldDelay[c] ? history[base+(sample-oldDelay[c])%capacity] : 0;
                const double newValue=sample>=newDelay[c] ? history[base+(sample-newDelay[c])%capacity] : 0;
                output[c][f]=oldValue+(newValue-oldValue)*blend;
            }
        }
        ramp=(std::min)(fadeFrames,ramp+blockFrames);
    }
    bool settled() const noexcept {return ramp>=fadeFrames;}
    std::uint64_t processedFrames() const noexcept {return position;}
};
