#include "stdafx.h"
#include "FilterEngine.h"
#include "helpers/LogHelper.h"
#include <iostream>
#include <cmath>
#include <vector>

// Loads an explicit configuration through the production parser and factories.
// Offline PCM only: never changes the installed configuration or audio services.
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    const unsigned rate=std::stoul(argv[2]),frames=480;
    if(rate!=44100 && rate!=48000 && rate!=96000)return 2;
    LogHelper::set(stderr,true,true,false);
    try {
        FilterEngine engine;engine.initialize(float(rate),2,2,2,3,frames,argv[1]);
        std::vector<double> l(frames),r(frames),x(frames),y(frames);
        double* input[]={l.data(),r.data()};double* output[]={x.data(),y.data()};
        bool finite=true,available=true;double sums[2]{};unsigned counts[2]{};
        for(unsigned phase=0;phase<2;++phase){
            if(phase)engine.loadConfig(argv[1]);
            for(unsigned block=0;block<120;++block){
                for(unsigned f=0;f<frames;++f){
                    const double t=double(block*frames+f)/rate;
                    l[f]=.001*(std::sin(6.283185307179586*97*t)+std::sin(6.283185307179586*1000*t));
                    r[f]=.001*(std::sin(6.283185307179586*211*t)+std::sin(6.283185307179586*3000*t));
                }
                engine.process(output,input,frames);available &= engine.getAnalysisStatus()==0;
                for(unsigned f=0;f<frames;++f){finite &= std::isfinite(x[f]) && std::isfinite(y[f]);if(block>=100){sums[phase]+=x[f]*x[f]+y[f]*y[f];counts[phase]+=2;}}
            }
        }
        const double first=std::sqrt(sums[0]/counts[0]),reloaded=std::sqrt(sums[1]/counts[1]);
        const bool passed=finite && available && first>1e-10 && reloaded>1e-10;
        std::cout<<"{\"status\":\""<<(passed?"PASS":"FAIL")<<"\",\"rate\":"<<rate
            <<",\"finite\":"<<(finite?"true":"false")<<",\"processors_available\":"<<(available?"true":"false")
            <<",\"baseline_rms\":"<<first<<",\"reload_rms\":"<<reloaded
            <<",\"limits\":\"Offline production FilterEngine PCM; no installed audiodg or device certification\"}\n";
        return passed?0:1;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
