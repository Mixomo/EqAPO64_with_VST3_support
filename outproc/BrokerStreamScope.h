#pragma once
#include <windows.h>
#include <objbase.h>
#include <string>
#include <unordered_map>

namespace EqApoBroker {
// Owned by one FilterEngine. Consulted only while loading configuration,
// never from process(). Concurrent audible generations remain isolated. Only
// a module bypassed in the immediately preceding configuration can reclaim
// its retired worker, with identical plugin/user and channel layout.
// The retired worker applies the newly serialized preset before restarting.
class StreamScope {
    struct Entry {std::wstring id,signature;std::uint64_t generation=0,present=0;bool bypassed=false;};
    std::unordered_map<std::wstring,Entry> instances;
    std::uint64_t generation=0;
public:
    StreamScope()=default;
    StreamScope(const StreamScope&)=delete;
    StreamScope& operator=(const StreamScope&)=delete;
    // Configuration-owner operation: coexistence during crossfade needs distinct workers.
    void beginGeneration() {
        ++generation;
    }
    void markBypassed(const std::wstring& module) {
        const auto found=instances.find(module);
        if(found!=instances.end()){
            found->second.present=generation;
            if(found->second.generation!=generation)found->second.bypassed=true;
        }
    }
    void endGeneration() {
        for(auto it=instances.begin();it!=instances.end();) {
            if(it->second.present!=generation)it=instances.erase(it);else ++it;
        }
    }
    std::wstring instanceFor(const std::wstring& module,const std::wstring& signature=L"") {
        if(module.empty())return {};
        const auto found=instances.find(module);
        if(found!=instances.end() && found->second.signature==signature
            && (found->second.generation==generation || found->second.bypassed
                || generation-found->second.generation==2)) {
            found->second.generation=generation;found->second.present=generation;
            found->second.bypassed=false;return found->second.id;
        }
        GUID value{};wchar_t text[40]{};
        if(FAILED(CoCreateGuid(&value)) || !StringFromGUID2(value,text,40))return {};
        std::wstring id(text+1,36);instances[module]={id,signature,generation,generation,false};return id;
    }
};
}
