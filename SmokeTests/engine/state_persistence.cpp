#include "stdafx.h"
#include "helpers/VSTPluginLibrary.h"
#include "helpers/VSTPluginInstance.h"
#include "outproc/OutProcControlEndpoint.h"
#include "outproc/VSTStateHandoff.h"
#include "EqApoOutProcHost/NativeDspViewAudit.h"
#include <iostream>
#include <memory>
#include <cmath>
#include <stdexcept>

static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static double value(VSTPluginInstance& effect,unsigned id){for(const auto& p:effect.getParameterMetadata())if(p.id==id)return p.normalizedValue;throw std::runtime_error("parameter missing");}
static void expect(VSTPluginInstance& effect,unsigned id,double expected,const char* message){check(std::abs(value(effect,id)-expected)<1e-5,message);}
int wmain(int argc,wchar_t** argv){
    if(argc<3)return 2;
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);struct Apartment{~Apartment(){CoUninitialize();}} apartment;
    try {
        OutProcVSTConfig initial;initial.libraryPath=argv[1];const unsigned id=std::stoul(argv[2]);
        auto library=VSTPluginLibrary::getInstance(initial.libraryPath);check(library->initialize()>=0,"load plugin");
        auto effect=std::make_unique<VSTPluginInstance>(library,2);check(effect->initialize(),"initialize plugin");
        effect->setUsedChannelCount(6);effect->prepareForProcessing(48000,480,false);effect->startProcessing();
        effect->readFromEffect(initial.chunkData,initial.paramMap);
        const auto original=value(*effect,id),edited=original<.5?.75:.25;
        const auto session=L"state-persistence-"+std::to_wstring(GetCurrentProcessId()),sid=EqApoControl::currentUserSid();
        const auto identity=EqApoControl::controlIdentity(initial.libraryPath,L"",0);
        EqApoControl::ControlEndpointOwner owner;check(owner.create(session,sid,identity),"create endpoint");
        auto* channel=static_cast<EqApoControl::NativeViewChannel*>(MapViewOfFile(owner.getNativeViewHandle(),FILE_MAP_ALL_ACCESS,0,0,sizeof(EqApoControl::NativeViewChannel)));
        check(channel!=nullptr,"map endpoint");channel->editorPid.store(GetCurrentProcessId());
        EqApoControl::NativeViewChannel* replacement=nullptr;
        {
            NativeDspViewAudit panel(channel,session,initial);
            channel->desiredVisibility.store(1);channel->command.store(1);panel.service(effect.get());check(panel.getWindowForAudit()!=nullptr,"open native editor");
            effect->writeToEffect(L"",{{L"#"+std::to_wstring(id),float(edited)}});expect(*effect,id,edited,"edit applied");
            channel->command.store(3);panel.service(effect.get());check(!panel.getWindowForAudit(),"GUI closes");expect(*effect,id,edited,"closing GUI preserves parameter");
            OutProcVSTConfig saved;check(channel->snapshotSequence.load() && OutProcReadVSTConfig(channel->snapshotPath,saved),"complete checkpoint on GUI close");
            check(!saved.chunkData.empty(),"full preset checkpoint contains opaque state");
            auto restored=std::make_unique<VSTPluginInstance>(library,2);check(restored->initialize(),"fresh component");restored->setUsedChannelCount(6);
            restored->writeToEffect(saved.chunkData,saved.paramMap);restored->prepareForProcessing(48000,480,false);expect(*restored,id,edited,"fresh instance restores saved parameter");restored.reset();
            channel->desiredVisibility.store(1);channel->command.store(1);panel.service(effect.get());expect(*effect,id,edited,"reopening GUI preserves parameter");
            const auto window=panel.getWindowForAudit();channel->desiredVisibility.store(2);channel->command.store(2);panel.service(effect.get());check(!IsWindowVisible(window),"panel hidden");
            EqApoControl::ControlEndpointOwner next;check(next.create(session,sid,identity,48000,6,false),"reactivation endpoint");
            replacement=static_cast<EqApoControl::NativeViewChannel*>(MapViewOfFile(next.getNativeViewHandle(),FILE_MAP_ALL_ACCESS,0,0,sizeof(EqApoControl::NativeViewChannel)));
            check(replacement!=nullptr,"map reactivation");
            effect->stopProcessing();
            if(OutProcVSTPresetChanged(initial,initial))effect->writeToEffect(initial.chunkData,initial.paramMap);
            effect->prepareForProcessing(48000,480,false);effect->startProcessing();
            expect(*effect,id,edited,"unchanged saved preset must not erase live GUI edits on reactivation");
            check(panel.rebind(replacement,session,identity),"rebind native editor");check(panel.getWindowForAudit()==window && !IsWindowVisible(window),"same hidden GUI retained");
            OutProcVSTConfig changed=initial;changed.chunkData.clear();changed.paramMap={{L"#"+std::to_wstring(id),float(original)}};
            check(OutProcVSTPresetChanged(initial,changed),"explicit preset change detected");effect->writeToEffect(changed.chunkData,changed.paramMap);expect(*effect,id,original,"explicit preset change applies");
            panel.close();
        }
        if(replacement)UnmapViewOfFile(replacement);
        UnmapViewOfFile(channel);std::cout<<"PASS close/reopen, full checkpoint/fresh instance, bypass/reactivation, hidden GUI, explicit preset change\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
