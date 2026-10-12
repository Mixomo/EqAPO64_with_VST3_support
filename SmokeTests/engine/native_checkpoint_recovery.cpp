#include "stdafx.h"
#include <fstream>
#include <iostream>
#include <memory>
#include <cmath>
#include "helpers/VSTPluginLibrary.h"
#include "helpers/VSTPluginInstance.h"
#include "outproc/OutProcControlEndpoint.h"
#include "outproc/OutProcVSTConfig.h"
#include "EqApoOutProcHost/NativeDspViewAudit.h"

static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int wmain(int argc,wchar_t** argv){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {
        if(argc==6 && std::wstring(argv[1])==L"--worker") {
            HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,argv[2]);
            if(!mapping && std::wstring(argv[2]).find(L"Global\\")==0)mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(L"Local\\"+std::wstring(argv[2]).substr(7)).c_str());
            check(mapping!=nullptr,"own native channel open");
            auto* channel=static_cast<EqApoControl::NativeViewChannel*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(EqApoControl::NativeViewChannel)));
            check(channel!=nullptr,"own native channel map");
            OutProcVSTConfig config;config.libraryPath=argv[3];
            if(channel->snapshotSequence.load())check(OutProcReadVSTConfig(channel->snapshotPath,config),"recover complete checkpoint");
            auto library=VSTPluginLibrary::getInstance(config.libraryPath);check(library->initialize()>=0,"fixture library load");
            auto effect=std::make_unique<VSTPluginInstance>(library,2);check(effect->initialize(),"fixture initialize");
            effect->setUsedChannelCount(2);effect->writeToEffect(config.chunkData,config.paramMap);effect->prepareForProcessing(44100,512,false);effect->startProcessing();
            auto view=std::make_unique<NativeDspViewAudit>(channel,argv[4],config);
            channel->command.store(1);view->service(effect.get());check(channel->status.load()==2,"real fixture view attached");
            const double gain=std::stod(argv[5]);check(effect->applyParameterChangeOnDsp(7,gain,0),"fixture gain edit");
            float input[512],output[512]{};for(auto& sample:input)sample=1;
            float* inputs[]={input,input};float* outputs[]={output,output};
            effect->processReplacing(inputs,outputs,512);effect->serviceControllerParameterChanges();
            check(std::abs(output[511]-gain)<1e-5,"recovered component processes edited gain");
            view->close();check(channel->snapshotSequence.load() && !channel->snapshotError.load(),"complete state published");
            // Deliberately simulate a worker exit without destructor cleanup.
            // Parent owns the channel and cleans up these exact private paths.
            ExitProcess(0);
        }
        check(argc==2,"fixture argument required");
        auto owner=std::make_unique<EqApoControl::ControlEndpointOwner>();
        const auto session=L"native-checkpoint-"+std::to_wstring(GetCurrentProcessId());
        check(owner->create(session,EqApoControl::currentUserSid(),12345),"create own generation");
        auto* channel=static_cast<EqApoControl::NativeViewChannel*>(MapViewOfFile(owner->getNativeViewHandle(),FILE_MAP_ALL_ACCESS,0,0,sizeof(EqApoControl::NativeViewChannel)));
        check(channel!=nullptr,"own channel");channel->editorPid.store(GetCurrentProcessId());
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        const auto object=L"Global\\EqApoControl_"+session+L"_"+EqApoControl::currentUserSid()+L"_"+std::to_wstring(owner->getGeneration())+L"_View";
        std::wstring firstPath;
        for(const auto* gain:{L"0.25",L"0.5"}) {
            std::wstring command=L"\""+std::wstring(executable)+L"\" --worker \""+object+L"\" \""+argv[1]+L"\" "+session+L" "+gain;
            STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
            check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE,"start own worker");
            const auto waited=WaitForSingleObject(child.hProcess,10000);DWORD code=1;
            if(waited!=WAIT_OBJECT_0)TerminateProcess(child.hProcess,99);
            GetExitCodeProcess(child.hProcess,&code);CloseHandle(child.hThread);CloseHandle(child.hProcess);
            if(waited!=WAIT_OBJECT_0 || code!=0)throw std::runtime_error("own worker failed: exit="+std::to_string(code));
            OutProcVSTConfig saved;check(OutProcReadVSTConfig(channel->snapshotPath,saved),"published file remains readable after worker exit");
            check(saved.paramMap.count(L"#7") && std::abs(saved.paramMap[L"#7"]-std::stod(gain))<1e-5,"latest state recovered after worker restart");
            if(firstPath.empty())firstPath=channel->snapshotPath;else check(firstPath==channel->snapshotPath,"published recovery path stays immutable");
        }
        check(channel->snapshotSequence.load()==2,"each forced checkpoint published exactly once");
        const auto directory=firstPath.substr(0,firstPath.find_last_of(L'\\'));
        check(DeleteFileW(firstPath.c_str())!=FALSE && RemoveDirectoryW(directory.c_str())!=FALSE,"own checkpoint cleaned");
        UnmapViewOfFile(channel);std::cout<<"PASS native full state / exited worker / recovery / stable path / final checkpoint / private fixture PCM\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
