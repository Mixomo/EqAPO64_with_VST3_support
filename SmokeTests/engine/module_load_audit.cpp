#include "stdafx.h"
#include "helpers/VSTPluginLibrary.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){
    if(argc!=2 && argc!=3)return 2;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    auto library=VSTPluginLibrary::getInstance(argv[1]);const auto result=library->initialize();
    bool combined=false;int controllerClassResult=-1;
    if(argc==3 && result>=0 && library->isVST3()){
        Steinberg::Vst::IComponent* component=nullptr;Steinberg::TUID iid;Steinberg::Vst::IComponent::iid.toTUID(iid);
        if(library->getFactory()->createInstance(library->getVST3ClassInfo().cid,iid,reinterpret_cast<void**>(&component))==Steinberg::kResultOk && component){
            auto* context=VSTPluginInstance::createFactoryHostContext();
            const bool initialized=component->initialize(context)==Steinberg::kResultOk;
            Steinberg::Vst::IEditController* controller=nullptr;
            combined=component->queryInterface(Steinberg::Vst::IEditController::iid,reinterpret_cast<void**>(&controller))==Steinberg::kResultOk && controller;
            Steinberg::TUID controllerId{};controllerClassResult=component->getControllerClassId(controllerId);
            if(controller)controller->release();if(initialized)component->terminate();component->release();context->release();
        }
    }
    std::cout<<"{\"result\":"<<result<<",\"load_library_ms\":"<<library->moduleLoadMilliseconds()
             <<",\"init_dll_factory_ms\":"<<library->moduleInitializeMilliseconds()<<",\"combined_controller\":"<<(combined?"true":"false")<<",\"controller_class_result\":"<<controllerClassResult<<"}\n";
    library.reset();CoUninitialize();return result<0?1:0;
}
