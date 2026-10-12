#pragma once
#include "OutProcControlEndpoint.h"
#include "OutProcAudioProtocol.h"
#include "OutProcVSTConfig.h"
#include <sstream>
#include <thread>

namespace EqApoBroker {
constexpr unsigned stateMagic=0x53425045,stateVersion=4;
constexpr std::size_t maxStateBytes=256*1024*1024;
struct alignas(64) StateHeader {
    unsigned magic=stateMagic,version=stateVersion;
    std::uint64_t generation=0,identity=0,bytes=0;
    unsigned sampleRate=0,channels=0,frames=0;
    std::atomic<unsigned> published{0},workerPid{0};
    std::atomic<std::uint64_t> creatorHeartbeat{0};
    std::uint64_t controlGeneration=0;
    wchar_t controlSession[97]{},instanceSession[97]{};
};
inline std::wstring prefix(const std::wstring& space,const std::wstring& id,const std::wstring& sid) {
    if((space!=L"Global" && space!=L"Local") || !EqApoControl::validSessionId(id) || EqApoControl::canonicalSid(sid).empty())return {};
    return space+L"\\EqApoBrokerAudio_"+id+L"_"+sid+L"_";
}
struct SessionHandles {
    HANDLE audio=nullptr,state=nullptr,request=nullptr,response=nullptr,shutdown=nullptr;
    StateHeader* metadata=nullptr;
    ~SessionHandles(){if(metadata)UnmapViewOfFile(metadata);for(auto h:{audio,state,request,response,shutdown})if(h)CloseHandle(h);}
    SessionHandles()=default;SessionHandles(const SessionHandles&)=delete;SessionHandles& operator=(const SessionHandles&)=delete;
};
class Owner:public SessionHandles {
    HANDLE heartbeatStop=nullptr;
    std::thread heartbeatThread;
public:
    std::wstring space,id,user;
    OutProcAudioHeader* header=nullptr;
    void stopHeartbeat(){if(heartbeatStop)SetEvent(heartbeatStop);if(heartbeatThread.joinable())heartbeatThread.join();if(heartbeatStop){CloseHandle(heartbeatStop);heartbeatStop=nullptr;}}
    ~Owner(){stopHeartbeat();if(metadata)metadata->published.store(0);if(shutdown)SetEvent(shutdown);if(header)UnmapViewOfFile(header);}
    bool create(const std::wstring& session,const std::wstring& userSid,const OutProcVSTConfig& config,unsigned rate,unsigned channels,unsigned frames,
        const std::wstring& controlSession=L"",std::uint64_t controlGeneration=0,const std::wstring& instanceSession=L"") {
        if(!instanceSession.empty() && (!controlGeneration || !EqApoControl::validSessionId(instanceSession)))return false;
        if(audio)return false;user=EqApoControl::canonicalSid(userSid);id=session;
        const auto creator=EqApoControl::canonicalSid(EqApoControl::currentUserSid());
        if(user.empty() || creator.empty() || !EqApoControl::validSessionId(id) || !rate)return false;
        if((controlGeneration && !EqApoControl::validSessionId(controlSession)) || (!controlGeneration && !controlSession.empty()))return false;
        std::ostringstream stream(std::ios::out|std::ios::binary);if(!OutProcWriteVSTConfigStream(stream,config))return false;
        const auto payload=stream.str();std::size_t audioBytes=0;
        if(payload.empty() || payload.size()>maxStateBytes || !OutProcAudioSharedMemorySize(channels,frames,audioBytes))return false;
        PSECURITY_DESCRIPTOR descriptor=nullptr;const auto acl=L"D:P(A;;GA;;;SY)(A;;GA;;;"+creator+L")(A;;GA;;;"+user+L")";
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return false;
        struct Free {PSECURITY_DESCRIPTOR p;~Free(){LocalFree(p);}} free{descriptor};SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
        space=L"Global";auto base=prefix(space,id,user);
        state=CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,0,DWORD(sizeof(StateHeader)+payload.size()),(base+L"State").c_str());
        if(!state && GetLastError()==ERROR_ACCESS_DENIED){DWORD sessionId=0;if(!ProcessIdToSessionId(GetCurrentProcessId(),&sessionId) || sessionId==0)return false;
            space=L"Local";base=prefix(space,id,user);state=CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,0,DWORD(sizeof(StateHeader)+payload.size()),(base+L"State").c_str());}
        if(!state || GetLastError()==ERROR_ALREADY_EXISTS)return false;
        metadata=static_cast<StateHeader*>(MapViewOfFile(state,FILE_MAP_ALL_ACCESS,0,0,sizeof(StateHeader)+payload.size()));if(!metadata)return false;
        new(metadata) StateHeader();GUID unique{};if(FAILED(CoCreateGuid(&unique)))return false;memcpy(&metadata->generation,&unique,8);if(!metadata->generation)metadata->generation=1;
        metadata->identity=EqApoControl::controlIdentity(config.libraryPath,config.classUID,config.vst3ClassIndex);
        metadata->bytes=payload.size();metadata->sampleRate=rate;metadata->channels=channels;metadata->frames=frames;
        metadata->controlGeneration=controlGeneration;wcscpy_s(metadata->controlSession,controlSession.c_str());wcscpy_s(metadata->instanceSession,(instanceSession.empty()?controlSession:instanceSession).c_str());
        memcpy(metadata+1,payload.data(),payload.size());
        audio=CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,DWORD(audioBytes>>32),DWORD(audioBytes),(base+L"Audio").c_str());
        if(!audio || GetLastError()==ERROR_ALREADY_EXISTS)return false;
        request=CreateEventW(&attributes,FALSE,FALSE,(base+L"Request").c_str());if(!request || GetLastError()==ERROR_ALREADY_EXISTS)return false;
        response=CreateEventW(&attributes,FALSE,FALSE,(base+L"Response").c_str());if(!response || GetLastError()==ERROR_ALREADY_EXISTS)return false;
        shutdown=CreateEventW(&attributes,TRUE,FALSE,(base+L"Shutdown").c_str());if(!shutdown || GetLastError()==ERROR_ALREADY_EXISTS)return false;
        header=static_cast<OutProcAudioHeader*>(MapViewOfFile(audio,FILE_MAP_ALL_ACCESS,0,0,audioBytes));if(!header)return false;
        ZeroMemory(header,audioBytes);new(header) OutProcAudioHeader();header->magic=OUTPROC_AUDIO_MAGIC;header->version=OUTPROC_AUDIO_VERSION;
        header->sampleRate=rate;header->channelCount=channels;header->maxFrames=frames;header->dspType=OUTPROC_AUDIO_DSP_VST;
        header->sessionEpoch.store(metadata->generation);new(&header->parameters) EqApoControl::ParameterMailbox(metadata->generation);OutProcInitializeSlots(header);
        heartbeatStop=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!heartbeatStop)return false;
        metadata->creatorHeartbeat.store(GetTickCount64());
        try{heartbeatThread=std::thread([this]{while(WaitForSingleObject(heartbeatStop,500)==WAIT_TIMEOUT)metadata->creatorHeartbeat.store(GetTickCount64(),std::memory_order_release);});}
        catch(...){CloseHandle(heartbeatStop);heartbeatStop=nullptr;return false;}
        metadata->published.store(1,std::memory_order_release);return true;
    }
};
class Client:public SessionHandles {
    unsigned claimedPid=0;
    HANDLE leaseStop=nullptr;
    std::thread leaseThread;
public:
    OutProcVSTConfig config;
    void stopLeaseWatch(){if(leaseStop)SetEvent(leaseStop);if(leaseThread.joinable())leaseThread.join();if(leaseStop){CloseHandle(leaseStop);leaseStop=nullptr;}}
    ~Client(){stopLeaseWatch();if(metadata && claimedPid){unsigned expected=claimedPid;metadata->workerPid.compare_exchange_strong(expected,0);}}
    bool open(const std::wstring& space,const std::wstring& id) {
        const auto base=prefix(space,id,EqApoControl::currentUserSid());if(base.empty() || state)return false;
        state=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(base+L"State").c_str());if(!state)return false;
        metadata=static_cast<StateHeader*>(MapViewOfFile(state,FILE_MAP_ALL_ACCESS,0,0,0));if(!metadata)return false;
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(metadata,&info,sizeof(info)) || info.RegionSize<sizeof(StateHeader) || metadata->magic!=stateMagic || metadata->version!=stateVersion
            || !metadata->published.load(std::memory_order_acquire) || !metadata->generation || metadata->bytes>maxStateBytes || metadata->bytes>info.RegionSize-sizeof(StateHeader))return false;
        const std::string payload(reinterpret_cast<const char*>(metadata+1),std::size_t(metadata->bytes));std::istringstream stream(payload,std::ios::in|std::ios::binary);
        if(!OutProcReadVSTConfigStream(stream,config) || stream.peek()!=std::char_traits<char>::eof()
            || EqApoControl::controlIdentity(config.libraryPath,config.classUID,config.vst3ClassIndex)!=metadata->identity)return false;
        for(const auto& parameter:config.paramMap)if(!std::isfinite(parameter.second) || parameter.second<0 || parameter.second>1)return false;
        if(metadata->instanceSession[96] || (metadata->controlGeneration && !EqApoControl::validSessionId(metadata->instanceSession)) || (!metadata->controlGeneration && metadata->instanceSession[0]))return false;
        if(metadata->controlSession[96] || (metadata->controlGeneration && !EqApoControl::validSessionId(metadata->controlSession))
            || (!metadata->controlGeneration && metadata->controlSession[0]))return false;
        audio=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(base+L"Audio").c_str());
        request=OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,(base+L"Request").c_str());
        response=OpenEventW(EVENT_MODIFY_STATE,FALSE,(base+L"Response").c_str());shutdown=OpenEventW(SYNCHRONIZE,FALSE,(base+L"Shutdown").c_str());
        if(!audio || !request || !response || !shutdown)return false;
        std::size_t bytes=0;if(!metadata->sampleRate || !OutProcAudioSharedMemorySize(metadata->channels,metadata->frames,bytes))return false;
        auto* h=static_cast<OutProcAudioHeader*>(MapViewOfFile(audio,FILE_MAP_READ,0,0,bytes));if(!h)return false;
        const bool valid=h->magic==OUTPROC_AUDIO_MAGIC && h->version==OUTPROC_AUDIO_VERSION && h->sampleRate==metadata->sampleRate && h->channelCount==metadata->channels
            && h->maxFrames==metadata->frames && h->sessionEpoch.load()==metadata->generation && h->parameters.generation==metadata->generation;UnmapViewOfFile(h);if(!valid)return false;
        unsigned expected=0;claimedPid=GetCurrentProcessId();if(!metadata->workerPid.compare_exchange_strong(expected,claimedPid)){claimedPid=0;return false;}
        // Independent of SDK initialization and GUI responsiveness. Runs outside DSP.
        leaseStop=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!leaseStop)return false;
        try{leaseThread=std::thread([this]{
            while(WaitForSingleObject(leaseStop,250)==WAIT_TIMEOUT){
                const auto now=GetTickCount64(),beat=metadata->creatorHeartbeat.load(std::memory_order_acquire);
                if(!metadata->published.load(std::memory_order_acquire) || !beat || (now>=beat && now-beat>5000)
                    || WaitForSingleObject(shutdown,0)==WAIT_OBJECT_0){
                    // The graph may already be gone, or vendor initialization/teardown hung.
                    // Only this exact worker exits; never terminate the creator or another host.
                    if(WaitForSingleObject(leaseStop,2000)==WAIT_TIMEOUT)TerminateProcess(GetCurrentProcess(),24);
                    return;
                }
            }
        });}catch(...){CloseHandle(leaseStop);leaseStop=nullptr;return false;}
        return true;
    }
};
}
