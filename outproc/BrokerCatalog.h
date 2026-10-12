#pragma once
#include "BrokerAudioSession.h"

namespace EqApoBroker {
struct CatalogRequest {wchar_t id[97]{},space[7]{},controlHost[97]{},instanceHost[97]{};std::uint64_t generation=0,identity=0;};
struct CatalogSlot {std::atomic<unsigned> state{0};CatalogRequest request;};
struct CatalogData {std::atomic<unsigned> magic{0};unsigned version=3;CatalogSlot entries[64];};
class Catalog {
    HANDLE mapping=nullptr;
    CatalogData* data=nullptr;
public:
    ~Catalog(){if(data)UnmapViewOfFile(data);if(mapping)CloseHandle(mapping);}
    Catalog()=default;Catalog(const Catalog&)=delete;Catalog& operator=(const Catalog&)=delete;
    bool open(const std::wstring& space,const std::wstring& sid,bool create){
        const auto user=EqApoControl::canonicalSid(sid);
        if(mapping || user.empty() || (space!=L"Global" && space!=L"Local"))return false;
        const auto name=space+L"\\EqApoBrokerCatalog_"+user;
        bool fresh=false;
        if(create){
            const auto creator=EqApoControl::canonicalSid(EqApoControl::currentUserSid());if(creator.empty())return false;
            const auto acl=L"D:P(A;;GA;;;SY)(A;;GA;;;"+creator+L")(A;;GA;;;"+user+L")";
            PSECURITY_DESCRIPTOR sd=nullptr;
            if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&sd,nullptr))return false;
            SECURITY_ATTRIBUTES attributes{sizeof(attributes),sd,FALSE};
            mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,0,sizeof(CatalogData),name.c_str());
            fresh=mapping && GetLastError()!=ERROR_ALREADY_EXISTS;LocalFree(sd);
        }else mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());
        if(!mapping)return false;
        data=static_cast<CatalogData*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(CatalogData)));if(!data)return false;
        if(fresh){new(data) CatalogData();data->magic.store(0x43425045,std::memory_order_release);}
        else for(unsigned i=0;i<100 && !data->magic.load(std::memory_order_acquire);++i)Sleep(10);
        return data->magic.load(std::memory_order_acquire)==0x43425045 && data->version==3;
    }
    bool publish(const Owner& owner){
        if(!data || !owner.metadata || !owner.metadata->published.load() || owner.id.size()>96)return false;
        for(auto& slot:data->entries){unsigned empty=0;if(!slot.state.compare_exchange_strong(empty,1))continue;
            slot.request={};wcscpy_s(slot.request.id,owner.id.c_str());wcscpy_s(slot.request.space,owner.space.c_str());
            wcscpy_s(slot.request.controlHost,owner.metadata->controlSession);wcscpy_s(slot.request.instanceHost,owner.metadata->instanceSession);slot.request.identity=owner.metadata->identity;
            slot.request.generation=owner.metadata->generation;slot.state.store(2,std::memory_order_release);return true;
        }return false;
    }
    bool take(CatalogRequest& request){
        if(!data)return false;
        for(auto& slot:data->entries){unsigned ready=2;if(!slot.state.compare_exchange_strong(ready,3,std::memory_order_acquire))continue;
            request=slot.request;slot.state.store(0,std::memory_order_release);
            if(request.id[96] || request.space[6] || request.controlHost[96] || request.instanceHost[96] || (request.controlHost[0] && !EqApoControl::validSessionId(request.instanceHost)) || !request.generation
                || (request.controlHost[0] && (!request.identity || !EqApoControl::validSessionId(request.controlHost)))
                || prefix(request.space,request.id,EqApoControl::currentUserSid()).empty())continue;
            return true;
        }return false;
    }
};
}
