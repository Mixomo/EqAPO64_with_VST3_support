#include "outproc/OutProcControlEndpoint.h"
#include <aclapi.h>
#include <iostream>
#include <memory>
#include <stdexcept>
static void check(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
int wmain(int argc,wchar_t** argv) {
    using namespace EqApoControl;
    if(argc==3 && std::wstring(argv[1])==L"--publish")return publishControl(argv[2],12345,7,.25,17)?0:1;
    try {
        const auto sid=currentUserSid(),session=L"owned-control-"+std::to_wstring(GetCurrentProcessId());
        auto original=std::make_unique<ControlEndpointOwner>();
        check(original->create(session,sid,12345),"create scoped endpoint");
        ControlPublisher cached(session,12345);
        check(cached.publish(90,.125),"cached publisher initialized");
        ParameterEvent cachedEvent;check(original->consume(cachedEvent) && cachedEvent.id==90,"cached event delivered");
        check(!publishControl(session,54321,7,.25,17),"wrong plugin identity rejected");
        wchar_t executable[32768]{};check(GetModuleFileNameW(nullptr,executable,32768)>0,"own child executable");
        std::wstring command=L"\""+std::wstring(executable)+L"\" --publish "+session;
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"create separate publisher");
        const auto waited=WaitForSingleObject(process.hProcess,5000);DWORD exitCode=1;
        if(waited!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,99);
        GetExitCodeProcess(process.hProcess,&exitCode);CloseHandle(process.hThread);CloseHandle(process.hProcess);
        check(waited==WAIT_OBJECT_0 && exitCode==0,"separate publisher completed bounded");
        ParameterEvent event;check(original->consume(event),"consume actual named mapping");
        check(event.id==7 && event.normalized==.25 && event.sampleOffset==17 && event.generation==original->getGeneration(),"payload/generation exact");
        auto* previousView=static_cast<NativeViewChannel*>(MapViewOfFile(original->getNativeViewHandle(),FILE_MAP_ALL_ACCESS,0,0,sizeof(NativeViewChannel)));
        check(previousView!=nullptr,"map original GUI");previousView->editorPid.store(GetCurrentProcessId());
        std::uint64_t checkpoint=0;std::wstring checkpointPath;
        check(!original->readNativeSnapshot(checkpoint,checkpointPath),"unpublished checkpoint ignored");
        wcscpy_s(previousView->snapshotPath,L"owned-checkpoint.opvs");
        previousView->snapshotSequence.store(1,std::memory_order_release);
        check(original->readNativeSnapshot(checkpoint,checkpointPath) && checkpoint==1 && checkpointPath==L"owned-checkpoint.opvs","complete native recovery checkpoint read");
        check(!original->readNativeSnapshot(checkpoint,checkpointPath),"unchanged checkpoint not reread");
        previousView->snapshotError.store(ERROR_WRITE_FAULT);previousView->snapshotSequence.store(2);
        check(!original->readNativeSnapshot(checkpoint,checkpointPath) && checkpoint==1,"failed checkpoint cannot replace recovery state");
        previousView->snapshotError.store(0);
        previousView->snapshotPath[1023]=L'x';
        check(!original->readNativeSnapshot(checkpoint,checkpointPath),"malformed checkpoint path rejected");
        previousView->snapshotPath[1023]=0;
        check(original->readNativeSnapshot(checkpoint,checkpointPath) && checkpoint==2,"repaired checkpoint can be retried");
        previousView->status.store(2);previousView->desiredVisibility.store(1);previousView->x.store(123);previousView->y.store(234);
        auto replacement=std::make_unique<ControlEndpointOwner>();check(replacement->create(session,sid,12345,48000,2,false),"stage replacement endpoint");
        check(replacement->getGeneration()!=original->getGeneration(),"new generation");
        check(cached.publish(92,.5),"edit during independent preparation");
        check(original->consume(cachedEvent) && cachedEvent.id==92,"audible predecessor receives edit immediately");
        check(replacement->consume(cachedEvent) && cachedEvent.id==92,"preparing replacement receives identical edit");
        check(publishControl(session,12345,93,.75,0,original->getGeneration()),"native predecessor edit during preparation");
        check(original->consume(cachedEvent) && cachedEvent.id==93,"native edit reaches predecessor");
        check(replacement->consume(cachedEvent) && cachedEvent.id==93,"native edit queued for replacement");
        replacement->activate();
        check(cached.publish(91,.875),"cached publisher follows generation");
        check(replacement->consume(cachedEvent) && cachedEvent.id==91 && cachedEvent.generation==replacement->getGeneration(),"cached event routes exclusively to replacement");
        auto* nextView=static_cast<NativeViewChannel*>(MapViewOfFile(replacement->getNativeViewHandle(),FILE_MAP_ALL_ACCESS,0,0,sizeof(NativeViewChannel)));
        check(nextView && nextView->predecessor==original->getGeneration(),"GUI predecessor retained");
        check(nextView->editorPid.load()==GetCurrentProcessId() && nextView->command.load()==1,"visible GUI lease inherited");
        check(nextView->x.load()==123 && nextView->y.load()==234,"GUI position inherited");
        check(!completeNativeViewTransfer(session,*nextView) && previousView->command.load()==0,"unattached replacement cannot close original GUI");
        nextView->status.store(2);check(completeNativeViewTransfer(session,*nextView),"attached replacement completes transfer");
        check(previousView->command.load()==3,"old view detaches only after replacement is visible");
        nextView->editorPid.store(0);previousView->command.store(0);
        check(!completeNativeViewTransfer(session,*nextView) && previousView->command.load()==0,"ownerless GUI cannot close predecessor");
        UnmapViewOfFile(nextView);UnmapViewOfFile(previousView);
        check(!publishControl(session,12345,33,.5,0,original->getGeneration()),"stale native GUI cannot automate replacement");
        check(!replacement->consume(event),"stale GUI edit leaves replacement mailbox unchanged");
        check(publishControl(session,12345,34,.5,0,replacement->getGeneration()),"exact live GUI generation accepted");
        check(replacement->consume(event) && event.id==34 && event.generation==replacement->getGeneration(),"exact generation delivered");
        original.reset();check(publishControl(session,12345,19,.75,0),"old owner cannot clear replacement");
        check(replacement->consume(event) && event.id==19,"route to replacement generation");
        PACL dacl=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
        check(GetSecurityInfo(replacement->getIndexHandleForAudit(),SE_KERNEL_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&dacl,nullptr,&descriptor)==ERROR_SUCCESS,"read actual DACL");
        check(dacl!=nullptr,"explicit protected DACL");
        for(DWORD i=0;i<dacl->AceCount;++i) {
            void* ace=nullptr;check(GetAce(dacl,i,&ace)!=FALSE,"read ACE");
            auto* allowed=static_cast<ACCESS_ALLOWED_ACE*>(ace);LPWSTR text=nullptr;
            check(ConvertSidToStringSidW(&allowed->SidStart,&text)!=FALSE,"ACE SID");
            const std::wstring permitted=text;LocalFree(text);
            check(permitted==sid || permitted==L"S-1-5-18","DACL limited to intended user/creator/SYSTEM");
        }LocalFree(descriptor);
        replacement.reset();check(!publishControl(session,12345,7,.5),"closed endpoint unavailable");
        {
            ControlEndpointOwner cold;check(cold.create(session,sid,12345,48000,2,false),"stage first cold generation");
            auto* view=static_cast<NativeViewChannel*>(MapViewOfFile(cold.getNativeViewHandle(),FILE_MAP_ALL_ACCESS,0,0,sizeof(NativeViewChannel)));
            check(view!=nullptr,"cold native channel");view->hostPid.store(GetCurrentProcessId());
            check(nativeViewControl(session,12345,1).available && view->command.load()==1,"first GUI request queued before wet activation");
            check(cached.publish(94,.5) && cold.consume(event) && event.id==94,"cold edit reaches pending generation");
            UnmapViewOfFile(view);
        }
        {
            const auto retainedSession=session+L"-retained";
            auto owner=std::make_unique<ControlEndpointOwner>();
            check(owner->create(retainedSession,sid,12345),"create retained panel endpoint");
            const auto generation=owner->getGeneration();
            HANDLE retainedHandle=nullptr;
            check(DuplicateHandle(GetCurrentProcess(),owner->getNativeViewHandle(),GetCurrentProcess(),&retainedHandle,0,FALSE,DUPLICATE_SAME_ACCESS),"worker holds the named mapping handle during bypass");
            auto* retained=static_cast<NativeViewChannel*>(MapViewOfFile(owner->getNativeViewHandle(),FILE_MAP_ALL_ACCESS,0,0,sizeof(NativeViewChannel)));
            check(retained!=nullptr,"worker retains its exact panel mapping");
            retained->hostPid.store(GetCurrentProcessId());retained->status.store(2);retained->desiredVisibility.store(1);
            owner.reset(); // Module bypass retires the current audio endpoint.
            check(nativeViewControl(retainedSession,12345,2,false,generation).available
                && retained->command.load()==2 && retained->desiredVisibility.load()==2,"hide retained GUI during bypass without current index");
            retained->command.store(0);retained->status.store(3);
            check(nativeViewControl(retainedSession,12345,1,false,generation).available
                && retained->desiredVisibility.load()==1,"show the same retained GUI during bypass");
            retained->command.store(0);retained->status.store(2);
            check(!nativeViewControl(retainedSession,54321,1,false,generation).available,"fallback identity remains validated");
            ControlEndpointOwner incoming;check(incoming.create(retainedSession,sid,12345,48000,2,false),"stage reactivation while retained view exists");
            auto* next=static_cast<NativeViewChannel*>(MapViewOfFile(incoming.getNativeViewHandle(),FILE_MAP_ALL_ACCESS,0,0,sizeof(NativeViewChannel)));
            check(next!=nullptr,"map pending reactivation");next->hostPid.store(GetCurrentProcessId());next->status.store(1);
            check(nativeViewControl(retainedSession,12345,3,false,generation).available
                && retained->command.load()==3 && next->command.load()==3
                && retained->desiredVisibility.load()==0 && next->desiredVisibility.load()==0,
                "close cancels retained and pending GUI rather than leaving an orphan");
            retained->status.store(1);retained->command.store(0);
            check(nativeViewControl(retainedSession,12345,0,false,generation).status==1,"X closure remains observable during bypass");
            const auto reopened=nativeViewControl(retainedSession,12345,1,false,generation);
            check(reopened.available && reopened.generation==incoming.getGeneration()
                && next->command.load()==1 && next->desiredVisibility.load()==1
                && retained->command.load()==0 && retained->desiredVisibility.load()==0,
                "X reopen targets current generation rather than a closed retired worker");
            UnmapViewOfFile(next);UnmapViewOfFile(retained);CloseHandle(retainedHandle);
        }
        ControlEndpointOwner invalid;check(!invalid.create(L"bad\\name",sid,1),"invalid object name rejected");
        check(!invalid.create(session,L"invalid SID",1),"invalid SID rejected");
        for(unsigned pendingShow=0;pendingShow<=1;++pendingShow) for(unsigned intent=0;intent<=2;++intent) {
            const auto leaseSession=session+L"-visibility-"+std::to_wstring(intent)+L"-"+std::to_wstring(pendingShow);
            ControlEndpointOwner before;check(before.create(leaseSession,sid,12345),"create visibility predecessor");
            auto* oldView=static_cast<NativeViewChannel*>(MapViewOfFile(before.getNativeViewHandle(),FILE_MAP_ALL_ACCESS,0,0,sizeof(NativeViewChannel)));
            check(oldView!=nullptr,"map visibility predecessor");
            oldView->editorPid.store(GetCurrentProcessId());oldView->hostPid.store(GetCurrentProcessId());
            // Window status can lag a close/hide request during graph replacement.
            oldView->status.store(2);oldView->desiredVisibility.store(intent);oldView->command.store(pendingShow);
            ControlEndpointOwner after;check(after.create(leaseSession,sid,12345,48000,2,false),"replace visibility generation");
            auto* next=static_cast<NativeViewChannel*>(MapViewOfFile(after.getNativeViewHandle(),FILE_MAP_ALL_ACCESS,0,0,sizeof(NativeViewChannel)));
            check(next && next->desiredVisibility.load()==intent,"replacement preserves user intent despite stale visible status");
            check(next->command.load()==(intent?1u:0u),"closed panel receives no automatic open request");
            UnmapViewOfFile(next);UnmapViewOfFile(oldView);
        }
        std::cout<<"PASS actual named control IPC / identity / replacement / cleanup / restricted DACL\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
