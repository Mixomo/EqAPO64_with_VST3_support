#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <string>
#include <vector>
#include <cstring>
#include <cwctype>
#include "OutProcParameterMailbox.h"
#include "OutProcMonitorMailbox.h"
#include <mutex>

namespace EqApoControl {

inline std::uint64_t controlIdentity(const std::wstring& path,const std::wstring& uid,int classIndex) {
    std::vector<wchar_t> full(32768);const auto length=GetFullPathNameW(path.c_str(),DWORD(full.size()),full.data(),nullptr);
    std::wstring key=length && length<full.size()?std::wstring(full.data(),length):path;
    key+=L"|"+(uid.empty()?std::to_wstring(classIndex):uid);
    std::uint64_t hash=14695981039346656037ull;
    for(auto c:key) {if(c==L'/')c=L'\\';c=wchar_t(std::towlower(c));hash^=std::uint16_t(c);hash*=1099511628211ull;}
    return hash;
}

inline std::wstring currentUserSid() {
    HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return {};
    DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
    std::vector<unsigned char> buffer(bytes);std::wstring result;
    if(bytes && GetTokenInformation(token,TokenUser,buffer.data(),bytes,&bytes)) {
        LPWSTR text=nullptr;
        if(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&text)) {result=text;LocalFree(text);}
    }
    CloseHandle(token);return result;
}

inline bool validSessionId(const std::wstring& id) {
    if(id.empty() || id.size()>96)return false;
    for(auto c:id)if(!((c>=L'a' && c<=L'z') || (c>=L'A' && c<=L'Z') || (c>=L'0' && c<=L'9') || c==L'-' || c==L'_'))return false;
    return true;
}

inline std::wstring canonicalSid(const std::wstring& text) {
    PSID sid=nullptr;LPWSTR canonical=nullptr;std::wstring result;
    if(ConvertStringSidToSidW(text.c_str(),&sid)) {
        if(IsValidSid(sid) && ConvertSidToStringSidW(sid,&canonical)) {result=canonical;LocalFree(canonical);}
        LocalFree(sid);
    }
    return result;
}

struct alignas(64) ControlIndex {
    std::atomic<std::uint32_t> magic{0};
    static constexpr std::uint32_t protocolVersion=2;
    std::uint32_t version=protocolVersion;
    std::uint64_t identity=0;
    std::atomic<std::uint64_t> generation{0};
    std::atomic<std::uint64_t> pendingGeneration{0};
};
constexpr std::uint32_t controlMagic=0x434F5045;

struct alignas(64) NativeViewChannel {
    static constexpr std::uint32_t magicValue=0x564E5045;
    static constexpr std::uint32_t protocolVersion=4;
    const std::uint32_t magic=magicValue,version=protocolVersion;
    const std::uint64_t generation,identity;
    std::atomic<unsigned> command{0},status{0},hostPid{0},editorPid{0};
    // User intent survives graph replacement: 0 closed, 1 visible, 2 hidden.
    std::atomic<unsigned> desiredVisibility{0};
    std::atomic<bool> inheritedVisibility{false};
    // Immutable predecessor plus a control-thread geometry snapshot.
    std::uint64_t predecessor=0;
    std::atomic<int> x{CW_USEDEFAULT},y{CW_USEDEFAULT},width{0},height{0};
    std::atomic<std::uint64_t> snapshotSequence{0};
    std::atomic<unsigned> snapshotError{0};
    wchar_t snapshotPath[1024]{};
    ParameterMailbox edits;
    NativeViewChannel(std::uint64_t gen,std::uint64_t id):generation(gen),identity(id),edits(gen){}
};

// Optional input samples and edits are shared only with the intended GUI user,
// creator and SYSTEM receive access; never Authenticated Users / Everyone.
class ControlEndpointOwner {
public:
    ControlEndpointOwner()=default;
    ControlEndpointOwner(const ControlEndpointOwner&)=delete;
    ControlEndpointOwner& operator=(const ControlEndpointOwner&)=delete;
    ~ControlEndpointOwner() {
        if(index && generation) {auto expected=generation;index->generation.compare_exchange_strong(expected,0,std::memory_order_acq_rel);
            expected=generation;index->pendingGeneration.compare_exchange_strong(expected,0,std::memory_order_acq_rel);}
        if(nativeView)UnmapViewOfFile(nativeView);if(nativeViewHandle)CloseHandle(nativeViewHandle);
        if(monitor)UnmapViewOfFile(monitor);if(monitorHandle)CloseHandle(monitorHandle);
        if(mailbox)UnmapViewOfFile(mailbox);if(index)UnmapViewOfFile(index);
        if(mailboxHandle)CloseHandle(mailboxHandle);if(indexHandle)CloseHandle(indexHandle);
    }
    bool create(const std::wstring& session,const std::wstring& guiSid,std::uint64_t identity,double monitorRate=48000,unsigned monitorChannels=2,bool publishNow=true) {
        if(index || !validSessionId(session))return false;
        const auto user=canonicalSid(guiSid),creator=canonicalSid(currentUserSid());
        if(user.empty() || creator.empty())return false;
        const auto acl=L"D:P(A;;GA;;;SY)(A;;GA;;;"+creator+L")(A;;GA;;;"+user+L")";
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return false;
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
        std::wstring prefix=L"Global\\EqApoControl_"+session+L"_"+user;
        indexHandle=CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,0,sizeof(ControlIndex),(prefix+L"_Current").c_str());
        if(!indexHandle && GetLastError()==ERROR_ACCESS_DENIED) {
            prefix=L"Local\\EqApoControl_"+session+L"_"+user;
            indexHandle=CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,0,sizeof(ControlIndex),(prefix+L"_Current").c_str());
        }
        const bool existing=indexHandle && GetLastError()==ERROR_ALREADY_EXISTS;
        if(!indexHandle) {LocalFree(descriptor);return false;}
        index=static_cast<ControlIndex*>(MapViewOfFile(indexHandle,FILE_MAP_ALL_ACCESS,0,0,sizeof(ControlIndex)));
        if(!index) {LocalFree(descriptor);return false;}
        if(existing) {
            if(index->magic.load(std::memory_order_acquire)!=controlMagic || index->version!=ControlIndex::protocolVersion || index->identity!=identity) {LocalFree(descriptor);return false;}
        } else {
            new(index) ControlIndex();index->identity=identity;
            index->magic.store(controlMagic,std::memory_order_release);
        }
        GUID unique{};if(FAILED(CoCreateGuid(&unique))) {LocalFree(descriptor);return false;}
        std::memcpy(&generation,&unique,sizeof(generation));if(!generation)generation=1;
        const auto previousGeneration=index->generation.load(std::memory_order_acquire);
        const auto name=prefix+L"_"+std::to_wstring(generation);
        mailboxHandle=CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,0,sizeof(ParameterMailbox),name.c_str());
        const bool mailboxExisting=mailboxHandle && GetLastError()==ERROR_ALREADY_EXISTS;
        nativeViewHandle=CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,0,sizeof(NativeViewChannel),(name+L"_View").c_str());
        const bool nativeExisting=nativeViewHandle && GetLastError()==ERROR_ALREADY_EXISTS;
        monitorHandle=CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,0,sizeof(MonitorMailbox),(name+L"_Monitor").c_str());
        LocalFree(descriptor);
        if(!nativeViewHandle || nativeExisting)return false;
        nativeView=static_cast<NativeViewChannel*>(MapViewOfFile(nativeViewHandle,FILE_MAP_ALL_ACCESS,0,0,sizeof(NativeViewChannel)));
        if(!nativeView)return false;new(nativeView) NativeViewChannel(generation,identity);
        if(previousGeneration) {
            HANDLE previousHandle=OpenFileMappingW(FILE_MAP_READ,FALSE,(prefix+L"_"+std::to_wstring(previousGeneration)+L"_View").c_str());
            auto* previous=previousHandle?static_cast<const NativeViewChannel*>(MapViewOfFile(previousHandle,FILE_MAP_READ,0,0,sizeof(NativeViewChannel))):nullptr;
            if(previous && previous->magic==NativeViewChannel::magicValue && previous->version==NativeViewChannel::protocolVersion
                && previous->identity==identity && previous->generation==previousGeneration) {
                const auto owner=previous->editorPid.load(std::memory_order_acquire);
                HANDLE editor=owner?OpenProcess(SYNCHRONIZE,FALSE,owner):nullptr;
                if(editor && WaitForSingleObject(editor,0)==WAIT_TIMEOUT) {
                    nativeView->predecessor=previousGeneration;
                    nativeView->editorPid.store(owner);
                    nativeView->x.store(previous->x.load());nativeView->y.store(previous->y.load());
                    nativeView->width.store(previous->width.load());nativeView->height.store(previous->height.load());
                    const auto pending=previous->command.load(std::memory_order_acquire);
                    auto visibility=previous->desiredVisibility.load(std::memory_order_acquire);
                    if(pending==3)visibility=0;
                    else if(pending==2)visibility=2;
                    // Window status is an asynchronous acknowledgement, not user
                    // intent. A stale "visible" status must not reopen a closed
                    // panel or override a hidden panel on graph replacement.
                    // A queued show is also obsolete if X/hide changed intent
                    // before that command was consumed by the old worker.
                    nativeView->desiredVisibility.store(visibility);
                    nativeView->inheritedVisibility.store(visibility!=0);
                    if(visibility)nativeView->command.store(1);
                }
                if(editor)CloseHandle(editor);
            }
            if(previous)UnmapViewOfFile(previous);if(previousHandle)CloseHandle(previousHandle);
        }
        if(!mailboxHandle || mailboxExisting)return false;
        mailbox=static_cast<ParameterMailbox*>(MapViewOfFile(mailboxHandle,FILE_MAP_ALL_ACCESS,0,0,sizeof(ParameterMailbox)));
        if(!mailbox)return false;
        new(mailbox) ParameterMailbox(generation);
        if(monitorHandle) {
            monitor=static_cast<MonitorMailbox*>(MapViewOfFile(monitorHandle,FILE_MAP_ALL_ACCESS,0,0,sizeof(MonitorMailbox)));
            if(monitor){new(monitor) MonitorMailbox(generation);monitor->sampleRate=monitorRate;monitor->channels=monitorChannels;}
        }
        if(publishNow)index->generation.store(generation,std::memory_order_release);
        else index->pendingGeneration.store(generation,std::memory_order_release);
        return true;
    }
    // Only preallocated atomics on the audio owner; publish after valid wet audio.
    void activate() noexcept {
        if(!index || index->generation.load(std::memory_order_acquire)==generation)return;
        if(index->pendingGeneration.load(std::memory_order_acquire)!=generation)return;
        index->generation.store(generation,std::memory_order_release);
        auto expected=generation;index->pendingGeneration.compare_exchange_strong(expected,0,std::memory_order_acq_rel);
    }
    bool consume(ParameterEvent& event) noexcept {return mailbox && mailbox->consume(generation,event);}
    std::uint64_t getGeneration() const noexcept {return generation;}
    std::uint64_t getDropped() const noexcept {return mailbox?mailbox->dropped.load(std::memory_order_relaxed):0;}
    HANDLE getNativeViewHandle() const noexcept {return nativeViewHandle;}
    bool readNativeSnapshot(std::uint64_t& sequence,std::wstring& path) const {
        if(!nativeView || nativeView->magic!=NativeViewChannel::magicValue
            || nativeView->version!=NativeViewChannel::protocolVersion
            || nativeView->generation!=generation || !index || nativeView->identity!=index->identity)return false;
        const auto next=nativeView->snapshotSequence.load(std::memory_order_acquire);
        if(!next || next==sequence || nativeView->snapshotError.load())return false;
        if(nativeView->snapshotPath[1023]!=0)return false;
        path=nativeView->snapshotPath;
        if(path.empty() || next!=nativeView->snapshotSequence.load(std::memory_order_acquire))return false;
        sequence=next;return true;
    }
    HANDLE getIndexHandleForAudit() const noexcept {return indexHandle;}
    void setMonitorFormat(double rate,unsigned count) noexcept {
        if(monitor){monitor->sampleRate=rate;monitor->channels=count;}
    }
    void publishMonitorInput(double** input,unsigned frames) noexcept {if(monitor)monitor->publish(input,frames);}
private:
    HANDLE indexHandle=nullptr,mailboxHandle=nullptr,nativeViewHandle=nullptr,monitorHandle=nullptr;
    MonitorMailbox* monitor=nullptr;
    NativeViewChannel* nativeView=nullptr;
    ControlIndex* index=nullptr;ParameterMailbox* mailbox=nullptr;
    std::uint64_t generation=0;
};

// GUI mirror worker only. Resolves handles here, never in the APO callback.
class MonitorReader {
    HANDLE currentHandle=nullptr,handle=nullptr;
    const ControlIndex* index=nullptr;
    MonitorMailbox* monitor=nullptr;
    std::wstring prefix;
    std::uint64_t identity=0,generation=0;
    bool ownsLease=false;
    void detach() {
        if(monitor){unsigned owner=GetCurrentProcessId();if(ownsLease)monitor->enabled.compare_exchange_strong(owner,0);UnmapViewOfFile(monitor);}
        if(handle)CloseHandle(handle);handle=nullptr;monitor=nullptr;generation=0;
        ownsLease=false;
    }
public:
    ~MonitorReader(){detach();if(index)UnmapViewOfFile(index);if(currentHandle)CloseHandle(currentHandle);}
    bool connect(const std::wstring& session,std::uint64_t pluginIdentity) {
        if(!validSessionId(session))return false;identity=pluginIdentity;
        const auto user=currentUserSid();if(user.empty())return false;
        for(const auto* space:{L"Global\\",L"Local\\"}) {
            prefix=std::wstring(space)+L"EqApoControl_"+session+L"_"+user;
            currentHandle=OpenFileMappingW(FILE_MAP_READ,FALSE,(prefix+L"_Current").c_str());
            if(!currentHandle)continue;
            index=static_cast<const ControlIndex*>(MapViewOfFile(currentHandle,FILE_MAP_READ,0,0,sizeof(ControlIndex)));
            if(index && index->magic.load()==controlMagic && index->version==ControlIndex::protocolVersion && index->identity==identity)return refresh();
            if(index)UnmapViewOfFile(index);CloseHandle(currentHandle);index=nullptr;currentHandle=nullptr;
        }
        return false;
    }
    bool refresh() {
        if(!index || index->identity!=identity)return false;
        const auto active=index->generation.load(std::memory_order_acquire);
        if(monitor && active==generation)return true;
        detach();if(!active)return false;
        handle=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(prefix+L"_"+std::to_wstring(active)+L"_Monitor").c_str());
        if(!handle)return false;
        monitor=static_cast<MonitorMailbox*>(MapViewOfFile(handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(MonitorMailbox)));
        if(!monitor || monitor->magic!=MonitorMailbox::magicValue || monitor->version!=1 || monitor->generation!=active
            || !monitor->channels || monitor->channels>64 || monitor->sampleRate<8000 || monitor->sampleRate>768000) {detach();return false;}
        unsigned owner=0;
        if(!monitor->enabled.compare_exchange_strong(owner,GetCurrentProcessId())){detach();return false;}
        ownsLease=true;generation=active;return true;
    }
    double sampleRate()const{return monitor?monitor->sampleRate:0;}
    unsigned channels()const{return monitor?monitor->channels:0;}
    unsigned consume(float* samples){return monitor?monitor->consume(samples):0;}
};

// A persistent GUI publisher avoids SID queries and repeated map/unmap calls
// for each mouse movement. The immutable index still guards every generation.
// GUI/control thread only. Keep the audible generation responsive and queue
// the same edits for its independently prepared replacement.
class ControlPublisher {
    std::wstring session,user,prefix;
    std::uint64_t identity=0;
    HANDLE indexHandle=nullptr;
    const ControlIndex* index=nullptr;
    struct Target {
        HANDLE handle=nullptr;ParameterMailbox* mailbox=nullptr;std::uint64_t generation=0;
        void clear(){if(mailbox)UnmapViewOfFile(mailbox);if(handle)CloseHandle(handle);mailbox=nullptr;handle=nullptr;generation=0;}
        ~Target(){clear();}
        bool publish(const std::wstring& prefix,std::uint64_t target,std::uint32_t id,double value,int offset){
            if(target!=generation){clear();if(!target)return false;
                handle=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(prefix+L"_"+std::to_wstring(target)).c_str());
                if(!handle)return false;
                mailbox=static_cast<ParameterMailbox*>(MapViewOfFile(handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(ParameterMailbox)));
                if(!mailbox){clear();return false;}generation=target;
            }
            return mailbox && mailbox->publish(target,id,value,offset);
        }
    } activeTarget,pendingTarget;
    std::mutex mutex;
public:
    ControlPublisher(const std::wstring& id,std::uint64_t key):session(id),user(currentUserSid()),identity(key){}
    ~ControlPublisher(){if(index)UnmapViewOfFile(index);if(indexHandle)CloseHandle(indexHandle);}
    bool publish(std::uint32_t id,double value,int offset=0,std::uint64_t expectedGeneration=0) {
        std::lock_guard<std::mutex> lock(mutex);
        if(!validSessionId(session) || user.empty())return false;
        if(!index)for(const auto* space:{L"Global\\",L"Local\\"}) {
            prefix=std::wstring(space)+L"EqApoControl_"+session+L"_"+user;
            indexHandle=OpenFileMappingW(FILE_MAP_READ,FALSE,(prefix+L"_Current").c_str());if(!indexHandle)continue;
            index=static_cast<const ControlIndex*>(MapViewOfFile(indexHandle,FILE_MAP_READ,0,0,sizeof(ControlIndex)));
            if(index && index->magic.load()==controlMagic && index->version==ControlIndex::protocolVersion && index->identity==identity)break;
            if(index)UnmapViewOfFile(index);CloseHandle(indexHandle);indexHandle=nullptr;index=nullptr;
        }
        if(!index || index->magic.load()!=controlMagic || index->identity!=identity)return false;
        const auto active=index->generation.load(std::memory_order_acquire);
        const auto pending=index->pendingGeneration.load(std::memory_order_acquire);
        if(expectedGeneration && expectedGeneration!=active && expectedGeneration!=pending)return false;
        const bool accepted=activeTarget.publish(prefix,active,id,value,offset);
        const bool queued=pendingTarget.publish(prefix,pending!=active?pending:0,id,value,offset);
        return accepted || queued;
    }
};

inline bool publishControl(const std::wstring& session,std::uint64_t identity,std::uint32_t id,double value,int offset=0,std::uint64_t expectedGeneration=0) {
    ControlPublisher publisher(session,identity);
    return publisher.publish(id,value,offset,expectedGeneration);
}
// Called only after the replacement has successfully attached its real view.
// Failure leaves the predecessor GUI and its DSP untouched.
inline bool completeNativeViewTransfer(const std::wstring& session,NativeViewChannel& replacement) {
    if(!validSessionId(session) || replacement.version!=NativeViewChannel::protocolVersion || !replacement.predecessor
        || (replacement.status.load()!=2 && replacement.status.load()!=3) || !replacement.editorPid.load())return false;
    const auto user=currentUserSid();if(user.empty())return false;
    for(const auto* space:{L"Global\\",L"Local\\"}) {
        const auto prefix=std::wstring(space)+L"EqApoControl_"+session+L"_"+user;
        HANDLE current=OpenFileMappingW(FILE_MAP_READ,FALSE,(prefix+L"_Current").c_str());
        const auto* index=current?static_cast<const ControlIndex*>(MapViewOfFile(current,FILE_MAP_READ,0,0,sizeof(ControlIndex))):nullptr;
        const bool active=index && index->magic.load()==controlMagic && index->version==ControlIndex::protocolVersion
            && index->identity==replacement.identity && index->generation.load()==replacement.generation;
        if(index)UnmapViewOfFile(index);if(current)CloseHandle(current);if(!active)continue;
        HANDLE handle=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(prefix+L"_"+std::to_wstring(replacement.predecessor)+L"_View").c_str());
        auto* previous=handle?static_cast<NativeViewChannel*>(MapViewOfFile(handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(NativeViewChannel))):nullptr;
        const bool compatible=previous && previous->magic==NativeViewChannel::magicValue && previous->version==NativeViewChannel::protocolVersion
            && previous->identity==replacement.identity && previous->generation==replacement.predecessor
            && previous->editorPid.load()==replacement.editorPid.load();
        if(compatible){previous->desiredVisibility.store(0);previous->command.store(3,std::memory_order_release);}
        if(previous)UnmapViewOfFile(previous);if(handle)CloseHandle(handle);return compatible;
    }
    return false;
}
// Editor/control thread only; exact immutable generation and plugin identity.
struct NativeViewSnapshot {bool available=false;unsigned status=0,pid=0,snapshotError=0,visibility=0;std::uint64_t generation=0,snapshotSequence=0;std::wstring snapshotPath;std::vector<ParameterEvent> edits;};
inline NativeViewSnapshot nativeViewControlGeneration(const std::wstring& session,std::uint64_t identity,
    std::uint64_t generation,unsigned command=0,bool consume=false) {
    NativeViewSnapshot result;if(!validSessionId(session))return result;
    if(!generation || command>3)return result;
    const auto user=currentUserSid();if(user.empty())return result;
    for(const auto* space:{L"Global\\",L"Local\\"}) {
        const auto prefix=std::wstring(space)+L"EqApoControl_"+session+L"_"+user;
        HANDLE handle=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(prefix+L"_"+std::to_wstring(generation)+L"_View").c_str());if(!handle)continue;
        auto* view=static_cast<NativeViewChannel*>(MapViewOfFile(handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(NativeViewChannel)));
        if(view && view->magic==NativeViewChannel::magicValue && view->version==NativeViewChannel::protocolVersion && view->generation==generation && view->identity==identity) {
            result.pid=view->hostPid.load();result.status=view->status.load();result.visibility=view->desiredVisibility.load();
            result.generation=generation;result.snapshotError=view->snapshotError.load();
            result.snapshotSequence=view->snapshotSequence.load(std::memory_order_acquire);
            if(result.snapshotSequence && !view->snapshotPath[1023])result.snapshotPath=view->snapshotPath;
            HANDLE host=result.pid?OpenProcess(SYNCHRONIZE,FALSE,result.pid):nullptr;
            result.available=host && WaitForSingleObject(host,0)==WAIT_TIMEOUT && result.status!=4;
            if(host)CloseHandle(host);
            if(result.available && (command || consume)) {
                unsigned owner=0;const auto pid=GetCurrentProcessId();
                if(view->editorPid.compare_exchange_strong(owner,pid) || owner==pid) {
                    // The foreground editor delegates activation to this exact VST worker
                    // before it receives the asynchronous show request.
                    if(command==1 && result.pid)AllowSetForegroundWindow(result.pid);
                    if(command)view->inheritedVisibility.store(false,std::memory_order_release);
                    if(command>=1 && command<=3)view->desiredVisibility.store(command==1?1u:command==2?2u:0u,std::memory_order_release);
                    if(command)view->command.store(command);
                    if(consume) {ParameterEvent event;for(unsigned n=0;n<256 && view->edits.consume(generation,event);++n)result.edits.push_back(event);}
                    if(command==3)view->editorPid.store(0);
                } else result.available=false;
            }
        }
        if(view)UnmapViewOfFile(view);CloseHandle(handle);return result;
    }
    return result;
}

inline NativeViewSnapshot nativeViewControl(const std::wstring& session,std::uint64_t identity,
    unsigned command=0,bool consume=false,std::uint64_t retainedGeneration=0) {
    NativeViewSnapshot result;if(!validSessionId(session))return result;
    const auto user=currentUserSid();if(user.empty())return result;
    std::uint64_t generation=0;
    for(const auto* space:{L"Global\\",L"Local\\"}) {
        const auto prefix=std::wstring(space)+L"EqApoControl_"+session+L"_"+user;
        HANDLE handle=OpenFileMappingW(FILE_MAP_READ,FALSE,(prefix+L"_Current").c_str());if(!handle)continue;
        auto* index=static_cast<const ControlIndex*>(MapViewOfFile(handle,FILE_MAP_READ,0,0,sizeof(ControlIndex)));
        if(index && index->magic.load()==controlMagic && index->version==ControlIndex::protocolVersion && index->identity==identity){
            generation=index->generation.load(std::memory_order_acquire);
            if(!generation)generation=index->pendingGeneration.load(std::memory_order_acquire);
        }
        if(index)UnmapViewOfFile(index);CloseHandle(handle);if(generation)break;
    }
    // Bypass retires the audio endpoint, but an open or hidden native panel
    // still owns its exact secured generation mapping in the worker.
    auto current=nativeViewControlGeneration(session,identity,generation);
    auto retained=retainedGeneration!=generation?nativeViewControlGeneration(session,identity,retainedGeneration):current;
    // Retain the generation only while it still owns an open/hidden panel or
    // an explicit pending visibility request. A closed retired worker must
    // not receive a reopen intended for the current audible generation.
    const bool retainedPanel=retained.available &&
        (retained.status==2 || retained.status==3 || retained.visibility!=0);
    if(current.available && (!retainedPanel || current.status==2 || current.status==3 || current.status==6))result=current;
    else if(retained.available)result=retained;
    else result=current;
    if(command==2 || command==3) {
        // Cancel visible intent on both sides of an in-flight handoff so a
        // pending replacement cannot reopen a panel the user just hid/closed.
        if(current.available)nativeViewControlGeneration(session,identity,current.generation,command);
        if(retained.available && retained.generation!=current.generation)nativeViewControlGeneration(session,identity,retained.generation,command);
    }
    if(result.available)return nativeViewControlGeneration(session,identity,result.generation,
        (command==2 || command==3)?0:command,consume);
    return result;
}

}
