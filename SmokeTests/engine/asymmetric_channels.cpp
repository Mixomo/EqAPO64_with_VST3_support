#include "stdafx.h"
#include "helpers/VSTPluginLibrary.h"
#include "helpers/VSTPluginInstance.h"
#include "outproc/BrokerAudioSession.h"
#include "outproc/AsyncAudioClient.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <stdexcept>

static void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
int wmain(int argc,wchar_t** argv){
    if(argc!=4 && argc!=5)return 2;
    const std::wstring mode=argc==5?argv[4]:L"decoder";
    const bool routing=argc==5;
    if(routing)SetEnvironmentVariableW(L"EQAPO_FIXTURE_LAYOUT",mode.c_str());
    const auto inputCount=[&](unsigned channels){return mode==L"fixed-surround"?6u:(mode==L"input-only" || mode==L"symmetric")?channels:2u;};
    const auto outputCount=[&](unsigned channels){return mode==L"fixed-surround"?6u:mode==L"decoder"?(channels==8?8u:6u):(mode==L"input-only" || mode==L"fixed-stereo")?2u:channels;};
    const auto expectedDirect=[](unsigned c,unsigned inputs){return .1*(c%inputs+1)*.25*(c+1)/8;};
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    std::ofstream report(argv[3]);unsigned cases=0;
    try{
        auto library=VSTPluginLibrary::getInstance(argv[1]);require(library->initialize()>=0,"module load");
        for(unsigned channels:{2u,6u,8u})for(unsigned rate:{44100u,48000u,96000u})for(bool precise:{false,true}){
            VSTPluginInstance instance(library,4);require(instance.initialize(),"initialize");
            instance.setUsedChannelCount(channels);instance.writeToEffect(L"",{{L"#7",.25f}});
            instance.prepareForProcessing(float(rate),480,precise);
            std::cout<<"channels="<<channels<<" rate="<<rate<<" precision="<<(precise?64:32)<<" negotiated="<<instance.numInputs()<<"->"<<instance.numOutputs()<<'\n';
            const auto inputs=inputCount(channels),outputs=outputCount(channels);
            require(instance.isProcessingValid() && instance.numInputs()==int(inputs) && instance.numOutputs()==int(outputs),"independent bus negotiation");
            instance.startProcessing();
            for(unsigned frames:{32u,128u,480u}){
                double in[8][480]{},out[8][480]{};double* ip[8];double* op[8];
                float fin[8][480]{},fout[8][480]{};float* fip[8];float* fop[8];
                for(unsigned c=0;c<8;++c){ip[c]=in[c];fip[c]=fin[c];op[c]=out[c];fop[c]=fout[c];for(unsigned f=0;f<frames;++f)in[c][f]=fin[c][f]=float(.1*(c+1));}
                if(precise)instance.processDoubleReplacing(ip,op,frames);else instance.processReplacing(fip,fop,frames);
                for(unsigned c=0;c<outputs;++c)for(unsigned f=0;f<frames;++f){
                    const double value=precise?out[c][f]:fout[c][f];
                    require(std::isfinite(value) && std::abs(value-expectedDirect(c,inputs))<1e-6,"direct channel mapping");
                }
            }
            instance.stopProcessing();++cases;
        }
        {
            VSTPluginInstance retained(library,4);require(retained.initialize(),"transition initialize");
            retained.writeToEffect(L"",{{L"#7",.25f}});
            for(unsigned channels:{2u,6u,8u,2u}){
                retained.setUsedChannelCount(channels);retained.prepareForProcessing(48000,32,true);
                const auto inputs=unsigned(retained.numInputs()),outputs=unsigned(retained.numOutputs());
                require(retained.isProcessingValid() && inputs==inputCount(channels)
                    && (mode==L"decoder"?(outputs==6 || outputs==8):outputs==outputCount(channels)),"device format transition");
                retained.startProcessing();
                double in[8][32]{},out[8][32]{};double* ip[8];double* op[8];
                for(unsigned c=0;c<8;++c){ip[c]=in[c];op[c]=out[c];for(auto& value:in[c])value=.1*(c+1);}
                retained.processDoubleReplacing(ip,op,32);
                for(unsigned c=0;c<outputs;++c)for(auto value:out[c])require(std::abs(value-expectedDirect(c,inputs))<1e-6,"transition PCM and retained gain");
                retained.stopProcessing();++cases;
            }
        }
        for(unsigned channels:{2u,6u,8u}){
            const auto inputs=inputCount(channels),outputs=outputCount(channels),stride=(std::max)(inputs,outputs);
            const auto expected=[&](unsigned c){const auto base=c/stride*stride,local=c%stride,source=base+local%inputs;
                if(local>=outputs || source>=channels || (!routing && source>=2))return 0.;
                return .1*(source+1)*.25*(local+1)/8;};
            GUID guid{};CoCreateGuid(&guid);wchar_t text[64]{};StringFromGUID2(guid,text,64);
            std::wstring id(text);id=id.substr(1,id.size()-2);
            OutProcVSTConfig config;config.libraryPath=argv[1];config.classUID=library->getVST3ClassUID(0);config.paramMap[L"#7"]=.25f;
            EqApoBroker::Owner session;
            require(session.create(id,EqApoControl::currentUserSid(),config,48000,channels,480),"session create");
            std::wstring command=L"\""+std::wstring(argv[2])+L"\" --broker-session "+id+L" --broker-space "+session.space;
            STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
            require(CreateProcessW(argv[2],command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE,"worker launch");
            CloseHandle(child.hThread);
            // Never touch the installed singleton broker or a user's worker.
            unsigned checks=0;bool valid=true;
            EqApoAsync::AudioClient audio;audio.prepare(channels,480,48000);
            double in[8][480]{},out[8][480]{};double* ip[8];double* op[8];
            for(unsigned c=0;c<channels;++c){ip[c]=in[c];op[c]=out[c];for(auto& value:in[c])value=(!routing && c>=2)?0:.1*(c+1);}
            for(unsigned block=0;block<200;++block){
                audio.process(op,ip,480,session.header);SetEvent(session.request);
                if(block>=150){++checks;for(unsigned c=0;c<channels;++c)for(unsigned f=0;f<480;++f)valid &= std::isfinite(out[c][f]) && std::abs(out[c][f]-expected(c))<1e-6;}
                Sleep(10);
            }
            SetEvent(session.shutdown);const auto waited=WaitForSingleObject(child.hProcess,5000);
            if(waited!=WAIT_OBJECT_0){TerminateProcess(child.hProcess,99);WaitForSingleObject(child.hProcess,1000);}
            DWORD exitCode=99;GetExitCodeProcess(child.hProcess,&exitCode);CloseHandle(child.hProcess);
            std::cout<<"worker channels="<<channels<<" consumed="<<session.header->consumed.load()<<" exit="<<exitCode<<" waited="<<waited<<" valid="<<valid<<'\n';
            for(unsigned c=0;c<channels;++c)std::cout<<"channel "<<c<<" sample="<<out[c][479]<<" expected="<<expected(c)<<'\n';
            require(valid && checks==50 && waited==WAIT_OBJECT_0 && exitCode==0,"worker multichannel PCM mapping");++cases;
        }
        report<<"{\"status\":\"PASS\",\"cases\":"<<cases<<",\"device_channels\":[2,6,8],\"limits\":\"Virtual SDK and own worker IPC; not installed audiodg or a physical endpoint\"}";
        std::cout<<"PASS "<<cases<<" asymmetric cases\n";return 0;
    }catch(const std::exception& error){report<<"{\"status\":\"FAIL\",\"reason\":\""<<error.what()<<"\"}";std::cerr<<error.what()<<'\n';return 1;}
}
