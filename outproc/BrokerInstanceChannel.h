#pragma once
#include "BrokerAudioSession.h"

namespace EqApoBroker {
constexpr unsigned instanceMagic=0x49425045;
struct InstanceRequest {wchar_t session[97]{},space[7]{};std::uint64_t transportGeneration=0,sequence=0;};
struct InstanceRequestSlot {std::atomic<unsigned> state{0};InstanceRequest request;};
struct InstanceIndex {
    std::atomic<unsigned> magic{0};unsigned version=1;
    std::uint64_t identity=0;
    std::atomic<std::uint64_t> generation{0};
    std::atomic<unsigned> workerPid{0};
};
struct InstanceData {
    unsigned magic=instanceMagic,version=1;
    std::uint64_t generation=0,identity=0;
    std::atomic<unsigned> accepting{0},workerPid{0};
    std::atomic<std::uint64_t> nextSequence{0},adoptedSequence{0},failedSequence{0};
    InstanceRequestSlot entries[8];
};
inline std::wstring instanceName(const std::wstring& host,std::uint64_t identity){
    const auto sid=EqApoControl::canonicalSid(EqApoControl::currentUserSid());
    if(sid.empty() || !identity || !EqApoControl::validSessionId(host))return {};
    return L"Local\\EqApoDspInstance_"+host+L"_"+sid+L"_"+std::to_wstring(identity);
}
class InstanceChannel {
    HANDLE guard=nullptr,indexHandle=nullptr,dataHandle=nullptr;
    InstanceIndex* index=nullptr;
    InstanceData* data=nullptr;
    bool owner=false;
    std::uint64_t generation=0;
public:
    InstanceChannel()=default;InstanceChannel(const InstanceChannel&)=delete;InstanceChannel& operator=(const InstanceChannel&)=delete;
    ~InstanceChannel(){
        if(owner && data){data->accepting.store(0,std::memory_order_release);auto expected=generation;index->generation.compare_exchange_strong(expected,0);}
        if(data)UnmapViewOfFile(data);if(index)UnmapViewOfFile(index);
        if(dataHandle)CloseHandle(dataHandle);if(indexHandle)CloseHandle(indexHandle);
        if(guard){if(owner)ReleaseMutex(guard);CloseHandle(guard);}
    }
    bool create(const std::wstring& host,std::uint64_t identity){
        const auto name=instanceName(host,identity);if(name.empty() || guard)return false;
        PSECURITY_DESCRIPTOR descriptor=nullptr;const auto acl=L"D:P(A;;GA;;;SY)(A;;GA;;;"+EqApoControl::currentUserSid()+L")";
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return false;
        struct Free{PSECURITY_DESCRIPTOR value;~Free(){LocalFree(value);}} free{descriptor};SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};
        guard=CreateMutexW(&security,FALSE,(name+L"_Owner").c_str());if(!guard)return false;
        const auto claimed=WaitForSingleObject(guard,0);if(claimed!=WAIT_OBJECT_0 && claimed!=WAIT_ABANDONED)return false;owner=true;
        indexHandle=CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,sizeof(InstanceIndex),(name+L"_Current").c_str());
        const bool fresh=indexHandle && GetLastError()!=ERROR_ALREADY_EXISTS;if(!indexHandle)return false;
        index=static_cast<InstanceIndex*>(MapViewOfFile(indexHandle,FILE_MAP_ALL_ACCESS,0,0,sizeof(InstanceIndex)));if(!index)return false;
        if(fresh){new(index) InstanceIndex();index->identity=identity;index->magic.store(instanceMagic,std::memory_order_release);}
        else if(index->magic.load(std::memory_order_acquire)!=instanceMagic || index->version!=1 || index->identity!=identity)return false;
        // Win32 mutexes are recursive: a second owner on the same thread must
        // still be rejected rather than replacing its live generation.
        if(index->generation.load(std::memory_order_acquire) && index->workerPid.load()==GetCurrentProcessId())return false;
        GUID unique{};if(FAILED(CoCreateGuid(&unique)))return false;memcpy(&generation,&unique,8);if(!generation)generation=1;
        dataHandle=CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,sizeof(InstanceData),(name+L"_"+std::to_wstring(generation)).c_str());
        if(!dataHandle || GetLastError()==ERROR_ALREADY_EXISTS)return false;
        data=static_cast<InstanceData*>(MapViewOfFile(dataHandle,FILE_MAP_ALL_ACCESS,0,0,sizeof(InstanceData)));if(!data)return false;
        new(data) InstanceData();data->generation=generation;data->identity=identity;data->workerPid.store(GetCurrentProcessId());
        data->accepting.store(1,std::memory_order_release);index->workerPid.store(GetCurrentProcessId());index->generation.store(generation,std::memory_order_release);return true;
    }
    bool open(const std::wstring& host,std::uint64_t identity){
        const auto name=instanceName(host,identity);if(name.empty() || dataHandle)return false;
        indexHandle=OpenFileMappingW(FILE_MAP_READ,FALSE,(name+L"_Current").c_str());if(!indexHandle)return false;
        index=static_cast<InstanceIndex*>(MapViewOfFile(indexHandle,FILE_MAP_READ,0,0,sizeof(InstanceIndex)));if(!index)return false;
        if(index->magic.load(std::memory_order_acquire)!=instanceMagic || index->version!=1 || index->identity!=identity)return false;
        generation=index->generation.load(std::memory_order_acquire);if(!generation)return false;
        dataHandle=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(name+L"_"+std::to_wstring(generation)).c_str());if(!dataHandle)return false;
        data=static_cast<InstanceData*>(MapViewOfFile(dataHandle,FILE_MAP_ALL_ACCESS,0,0,sizeof(InstanceData)));
        return data && data->magic==instanceMagic && data->version==1 && data->generation==generation && data->identity==identity
            && data->accepting.load(std::memory_order_acquire) && index->generation.load(std::memory_order_acquire)==generation;
    }
    std::uint64_t getGeneration()const noexcept{return generation;}
    unsigned getWorkerPid()const noexcept{return data?data->workerPid.load(std::memory_order_acquire):0;}
    bool submit(const std::wstring& session,const std::wstring& space,std::uint64_t transportGeneration,std::uint64_t expectedInstance){
        if(!data || !transportGeneration || expectedInstance!=generation || !data->accepting.load(std::memory_order_acquire)
            || index->generation.load(std::memory_order_acquire)!=generation || prefix(space,session,EqApoControl::currentUserSid()).empty())return false;
        for(auto& entry:data->entries){unsigned empty=0;if(!entry.state.compare_exchange_strong(empty,1))continue;
            if(!data->accepting.load(std::memory_order_acquire) || index->generation.load(std::memory_order_acquire)!=generation){entry.state.store(0,std::memory_order_release);return false;}
            entry.request={};wcscpy_s(entry.request.session,session.c_str());wcscpy_s(entry.request.space,space.c_str());
            entry.request.transportGeneration=transportGeneration;entry.request.sequence=data->nextSequence.fetch_add(1)+1;
            entry.state.store(2,std::memory_order_release);return true;
        }return false;
    }
    bool take(InstanceRequest& request){
        if(!owner || !data)return false;
        for(auto& entry:data->entries){unsigned ready=2;if(!entry.state.compare_exchange_strong(ready,3,std::memory_order_acquire))continue;
            request=entry.request;entry.state.store(0,std::memory_order_release);
            if(request.session[96] || request.space[6] || !request.transportGeneration || !request.sequence
                || prefix(request.space,request.session,EqApoControl::currentUserSid()).empty())continue;
            return true;
        }return false;
    }
    std::uint64_t getAdoptedSequence()const noexcept{return data?data->adoptedSequence.load(std::memory_order_acquire):0;}
    void acknowledge(std::uint64_t sequence,bool adopted){
        if(!owner || !data)return;
        auto& destination=adopted?data->adoptedSequence:data->failedSequence;
        auto previous=destination.load(std::memory_order_acquire);
        while(previous<sequence && !destination.compare_exchange_weak(previous,sequence,std::memory_order_release,std::memory_order_acquire)){}
    }
};
}
