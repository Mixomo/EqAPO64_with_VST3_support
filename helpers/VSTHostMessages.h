#pragma once
#include <atomic>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include "pluginterfaces/vst/ivstmessage.h"

// Control-thread services. These objects are never created by the APO callback.
namespace EqApoHost {
using namespace Steinberg;
using namespace Steinberg::Vst;
class Attributes final : public IAttributeList {
    struct Value { enum Type { Integer, Floating, Text, Binary } type;
        int64 integer=0; double floating=0; std::basic_string<TChar> text;
        std::vector<unsigned char> binary; };
    std::map<std::string,Value> values;
    std::atomic<uint32> refs{1};
    Value* find(AttrID id,Value::Type type) {
        if(!id) return nullptr; auto it=values.find(id);
        return it!=values.end() && it->second.type==type ? &it->second : nullptr;
    }
public:
    tresult PLUGIN_API queryInterface(const TUID iid,void** obj) override {
        if(!obj) return kInvalidArgument; *obj=nullptr;
        if(FUnknownPrivate::iidEqual(iid,FUnknown::iid)||FUnknownPrivate::iidEqual(iid,IAttributeList::iid)) {
            *obj=static_cast<IAttributeList*>(this);addRef();return kResultOk;
        } return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override {return ++refs;}
    uint32 PLUGIN_API release() override {auto n=--refs;if(!n)delete this;return n;}
    tresult PLUGIN_API setInt(AttrID id,int64 x) override {
        if(!id)return kInvalidArgument;Value v{};v.type=Value::Integer;v.integer=x;values[id]=std::move(v);return kResultOk;}
    tresult PLUGIN_API getInt(AttrID id,int64& x) override {
        auto v=find(id,Value::Integer);if(!v)return kResultFalse;x=v->integer;return kResultOk;}
    tresult PLUGIN_API setFloat(AttrID id,double x) override {
        if(!id)return kInvalidArgument;Value v{};v.type=Value::Floating;v.floating=x;values[id]=std::move(v);return kResultOk;}
    tresult PLUGIN_API getFloat(AttrID id,double& x) override {
        auto v=find(id,Value::Floating);if(!v)return kResultFalse;x=v->floating;return kResultOk;}
    tresult PLUGIN_API setString(AttrID id,const TChar* x) override {
        if(!id||!x)return kInvalidArgument;Value v{};v.type=Value::Text;v.text=x;values[id]=std::move(v);return kResultOk;}
    tresult PLUGIN_API getString(AttrID id,TChar* x,uint32 bytes) override {
        if(!x||bytes<sizeof(TChar))return kInvalidArgument;auto v=find(id,Value::Text);if(!v)return kResultFalse;
        auto count=(std::min)(v->text.size(),size_t(bytes/sizeof(TChar)-1));
        std::memcpy(x,v->text.data(),count*sizeof(TChar));x[count]=0;return kResultOk;}
    tresult PLUGIN_API setBinary(AttrID id,const void* x,uint32 bytes) override {
        if(!id||(!x&&bytes))return kInvalidArgument;Value v{};v.type=Value::Binary;
        if(bytes) {auto p=static_cast<const unsigned char*>(x);v.binary.assign(p,p+bytes);}
        values[id]=std::move(v);return kResultOk;}
    tresult PLUGIN_API getBinary(AttrID id,const void*& x,uint32& bytes) override {
        auto v=find(id,Value::Binary);if(!v)return kResultFalse;x=v->binary.data();bytes=uint32(v->binary.size());return kResultOk;}
};
class Message final : public IMessage {
    std::atomic<uint32> refs{1};std::string id;Attributes* attributes=new Attributes;
public:
    ~Message(){attributes->release();}
    tresult PLUGIN_API queryInterface(const TUID iid,void** obj) override {
        if(!obj)return kInvalidArgument;*obj=nullptr;
        if(FUnknownPrivate::iidEqual(iid,FUnknown::iid)||FUnknownPrivate::iidEqual(iid,IMessage::iid)) {
            *obj=static_cast<IMessage*>(this);addRef();return kResultOk;
        }return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override{return ++refs;}
    uint32 PLUGIN_API release() override{auto n=--refs;if(!n)delete this;return n;}
    FIDString PLUGIN_API getMessageID() override{return id.empty()?nullptr:id.c_str();}
    void PLUGIN_API setMessageID(FIDString value) override{id=value?value:"";}
    IAttributeList* PLUGIN_API getAttributes() override{return attributes;}
};
}
