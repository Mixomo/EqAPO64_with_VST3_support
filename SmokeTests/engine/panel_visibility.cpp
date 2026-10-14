#include "stdafx.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include "helpers/VSTPluginInstance.h"
#include "helpers/VSTPluginLibrary.h"
#include "outproc/OutProcControlEndpoint.h"
#include "outproc/OutProcVSTConfig.h"
#include "EqApoOutProcHost/NativeDspViewAudit.h"

static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int wmain(int argc,wchar_t** argv){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    struct Apartment {~Apartment(){CoUninitialize();}} apartment;
    try {
        check(argc==2,"supply a plug-in path or .opvs configuration");
        OutProcVSTConfig config;
        const std::wstring path=argv[1];
        const auto dot=path.find_last_of(L'.');
        if(dot!=std::wstring::npos && path.substr(dot)==L".opvs")check(OutProcReadVSTConfig(path,config),"read configuration");
        else config.libraryPath=path;
        auto library=VSTPluginLibrary::getInstance(config.libraryPath);
        check(library->initialize()>=0,"load plug-in");
        const auto session=L"panel-visibility-"+std::to_wstring(GetCurrentProcessId());
        const auto sid=EqApoControl::currentUserSid();
        const auto identity=EqApoControl::controlIdentity(config.libraryPath,config.classUID,config.vst3ClassIndex);
        EqApoControl::ControlEndpointOwner initial;check(initial.create(session,sid,identity),"initial endpoint");
        auto* old=static_cast<EqApoControl::NativeViewChannel*>(MapViewOfFile(initial.getNativeViewHandle(),FILE_MAP_ALL_ACCESS,0,0,sizeof(EqApoControl::NativeViewChannel)));
        check(old!=nullptr,"initial view mapping");
        {
            auto effect=std::make_unique<VSTPluginInstance>(library,2,config.vst3ClassIndex);
            check(effect->initialize(),"cold component initialization");effect->setUsedChannelCount(2);
            effect->writeToEffect(config.chunkData,config.paramMap);effect->prepareForProcessing(48000,128,false);effect->startProcessing();
            NativeDspViewAudit panel(old,session,config);
            for(unsigned tick=0;tick<10;++tick)panel.service(effect.get());
            check(panel.getWindowForAudit()==nullptr && old->command.load()==0,"cold startup without an editor cannot open a panel");
        }
        old->editorPid.store(GetCurrentProcessId());old->hostPid.store(GetCurrentProcessId());
        for(unsigned intent=0;intent<=2;++intent){
            // Simulate the old window acknowledgement lagging behind the user's intent.
            old->status.store(2);old->desiredVisibility.store(intent);old->command.store(0);
            EqApoControl::ControlEndpointOwner replacement;check(replacement.create(session,sid,identity,48000,2,false),"replacement endpoint");
            auto* next=static_cast<EqApoControl::NativeViewChannel*>(MapViewOfFile(replacement.getNativeViewHandle(),FILE_MAP_ALL_ACCESS,0,0,sizeof(EqApoControl::NativeViewChannel)));
            check(next && next->desiredVisibility.load()==intent,"inherited intent");
            auto effect=std::make_unique<VSTPluginInstance>(library,2,config.vst3ClassIndex);
            check(effect->initialize(),"initialize component");effect->setUsedChannelCount(2);
            effect->writeToEffect(config.chunkData,config.paramMap);effect->prepareForProcessing(48000,128,false);effect->startProcessing();
            {
                NativeDspViewAudit panel(next,session,config);panel.service(effect.get());
                const auto window=panel.getWindowForAudit();
                check(intent?window!=nullptr:window==nullptr,"closed generation stays closed; explicit open remains supported");
                if(window)check(bool(IsWindowVisible(window))==(intent==1),"hidden generation must not become visible");
                if(intent==0){next->command.store(1);next->desiredVisibility.store(1);next->inheritedVisibility.store(false);panel.service(effect.get());check(panel.getWindowForAudit()!=nullptr,"explicit open after closed replacement");}
                next->desiredVisibility.store(0);next->command.store(3);panel.service(effect.get());
                check(panel.getWindowForAudit()==nullptr,"close panel");
                for(unsigned tick=0;tick<10;++tick)panel.service(effect.get());
                check(panel.getWindowForAudit()==nullptr,"processing without a new show request cannot reopen panel");
            }
            effect->stopProcessing();effect.reset();UnmapViewOfFile(next);
        }
        UnmapViewOfFile(old);
        std::cout<<"PASS closed/visible/hidden generations, preset restoration, explicit open and close; isolated host only\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
