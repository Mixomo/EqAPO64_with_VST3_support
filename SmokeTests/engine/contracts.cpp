#include "stdafx.h"
#include "helpers/VSTPluginLibrary.h"
#include "helpers/RealtimeVST.h"
#include "helpers/VSTHostMessages.h"
#include "helpers/VSTPluginInstance.h"
#include "filters/PanFilter.h"
#include "filters/OutProcGainFilter.h"
#include "filters/OutProcBiquadFilter.h"
#include <memory>
#include "filters/VUMeterFilter.h"
#include "FilterEngine.h"
#include "FilterConfiguration.h"
#include "helpers/MemoryHelper.h"
#include <fstream>
#include <thread>
#include <iostream>
#include <cmath>
#include <stdexcept>

static void check(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
static void ringTest() {
    EqApoRT::SpscRing<unsigned,17> ring;std::atomic<bool> valid{true};
    std::thread producer([&]{for(unsigned i=0;i<1000000;++i)while(!ring.push(i))std::this_thread::yield();});
    std::thread consumer([&]{for(unsigned i=0;i<1000000;++i){unsigned value;while(!ring.pop(value))std::this_thread::yield();if(value!=i)valid=false;}});
    producer.join();consumer.join();check(valid,"SPSC ordering / wraparound");
    check(ring.empty(),"SPSC final drain");std::cout<<"PASS concurrent SPSC million events\n";
}
static void overflowTest() {
    EqApoRT::LatestParameterValues latest;std::vector<Steinberg::Vst::ParamID> ids;
    for(unsigned id=0;id<300;++id)ids.push_back(id);latest.prepare(ids);
    std::array<double,300> received{};
    std::thread producer([&]{for(unsigned round=0;round<1000;++round)for(unsigned id:ids)latest.remember(id,double(round)/1000,true);});
    producer.join();
    for(unsigned pass=0;pass<10;++pass)latest.drain(256,[&](auto id,double value){received[id]=value;return true;});
    for(auto value:received)check(value==0.999,"overflow must preserve final normalized value for every ID");
    std::cout<<"PASS overflow 300 distinct parameters / bounded drain\n";
}
static void hostMessageTest() {
    auto* message=new EqApoHost::Message;message->setMessageID("host-test");
    auto* attributes=message->getAttributes();
    Steinberg::int64 integer=0;double value=0;const void* binary=nullptr;Steinberg::uint32 size=0;
    check(attributes->setInt("integer",42)==Steinberg::kResultOk,"setInt");
    check(attributes->getInt("integer",integer)==Steinberg::kResultOk && integer==42,"getInt");
    check(attributes->getFloat("integer",value)!=Steinberg::kResultOk,"attribute type isolation");
    const char bytes[]={0,1,0,3};attributes->setBinary("binary",bytes,4);
    check(attributes->getBinary("binary",binary,size)==Steinberg::kResultOk && size==4 && !memcmp(bytes,binary,4),"binary roundtrip");
    attributes->setString("text",u"abcd");Steinberg::Vst::TChar text[2]={1,1};
    check(attributes->getString("text",text,sizeof(text))==Steinberg::kResultOk && text[0]==u'a' && text[1]==0,"UTF16 byte length and terminator");
    check(message->release()==0,"message ownership");std::cout<<"PASS VST3 message / attributes\n";
}
static void panTest() {
    double input[8][480]{},output[8][480]{};double* ip[8],*op[8];std::vector<std::wstring> names;
    for(unsigned c=0;c<8;++c){ip[c]=input[c];op[c]=output[c];names.push_back(std::to_wstring(c));for(unsigned f=0;f<480;++f)input[c][f]=std::sin(c+f*0.031);}
    PanFilter neutral(0,100);neutral.initialize(48000,480,names);neutral.process(op,ip,480);
    check(!memcmp(input,output,sizeof(input)),"neutral stereo and surround must be bit-exact");
    for(int pan:{-100,-50,0,50,100}) {
        for(unsigned f=0;f<480;++f)input[0][f]=input[1][f]=0.5;
        PanFilter filter(pan,100);filter.initialize(48000,480,{L"L",L"R"});filter.process(op,ip,480);
        check(std::abs(output[0][0]*output[0][0]+output[1][0]*output[1][0]-0.5)<1e-12,"centered mono constant-power pan");
    }
    std::cout<<"PASS pan exact neutral / constant power\n";
}
static void meterTest() {
    const auto id=L"engineering-contract-"+std::to_wstring(GetCurrentProcessId());
    VUMeterFilter first(id,L"all",L"AES17",L"ITU-R BS.1770-5",true);
    first.initialize(48000,480,{L"L",L"R"});
    if(first.getMappingError())std::cerr<<"VU mapping Win32 error="<<first.getMappingError()<<'\n';
    HANDLE mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,(L"Local\\EqAPO_VUMeter_v4_"+id).c_str());
    check(mapping!=nullptr,"VU shared mapping");
    auto* shared=static_cast<VUMeterSharedData*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(VUMeterSharedData)));
    check(shared!=nullptr,"VU map snapshot");
    double input[2][480],output[2][480];double* ip[]={input[0],input[1]},*op[]={output[0],output[1]};
    for(unsigned f=0;f<480;++f)input[0][f]=input[1][f]=0.5*std::sin(f*6.283185307179586*1000/48000);
    first.process(op,ip,480);VUMeterSnapshot snapshot{};CopyMeterSnapshot(shared,snapshot);
    check(!(snapshot.sequence&1) && snapshot.channelCount==2,"VU complete snapshot");
    check(std::abs(snapshot.peak[0]-0.5)<1e-12 && std::abs(snapshot.rms[0]-0.5)<1e-12,"VU peak and AES17 RMS");
    check(!memcmp(input,output,sizeof(input)),"VU transparent audio");
    const auto before=static_cast<std::uint64_t>(shared->sequence);
    VUMeterFilter replacement(id,L"all",L"AES17",L"ITU-R BS.1770-5",true);replacement.initialize(48000,480,{L"L",L"R"});
    const auto prepared=static_cast<std::uint64_t>(shared->sequence);check(prepared>before,"VU replacement publication");
    first.process(op,ip,480);check(static_cast<std::uint64_t>(shared->sequence)==prepared,"retired graph must not overwrite active meter");
    replacement.process(op,ip,480);check(static_cast<std::uint64_t>(shared->sequence)>prepared,"new graph owns meter");
    UnmapViewOfFile(shared);CloseHandle(mapping);std::cout<<"PASS VU peak / RMS / transparent audio / graph ownership\n";
}
class GraphDelayFixture final:public IFilter {
    unsigned delay;std::uint64_t position=0;double history[2][258]{};
public:
    explicit GraphDelayFixture(unsigned delay):delay(delay){}
    unsigned getLatencySamples() const noexcept override{return delay;}
    std::vector<std::wstring> initialize(float,unsigned,std::vector<std::wstring> names) override{return names;}
    void process(double** output,double** input,unsigned frames) override {
        for(unsigned f=0;f<frames;++f,++position)for(unsigned c=0;c<2;++c) {
            history[c][position%258]=input[c][f];
            output[c][f]=position>=delay?history[c][(position-delay)%258]:0;
        }
    }
};
static void graphTransitionTest() {
    const std::wstring path=L"R:/Temp/eqapo-graph-contract-"+std::to_wstring(GetCurrentProcessId())+L".txt";
    {std::ofstream empty(path);}
    FilterEngine engine;engine.initialize(48000,2,2,2,3,480,path);
    auto makeGraph=[&](unsigned delay) {
        auto* info=static_cast<FilterInfo*>(MemoryHelper::alloc(sizeof(FilterInfo)));
        info->filter=new(MemoryHelper::alloc(sizeof(GraphDelayFixture)))GraphDelayFixture(delay);
        info->inPlace=false;info->inChannelCount=info->outChannelCount=2;
        info->inChannels=static_cast<size_t*>(MemoryHelper::alloc(2*sizeof(size_t)));
        info->outChannels=static_cast<size_t*>(MemoryHelper::alloc(2*sizeof(size_t)));
        info->inChannels[0]=info->outChannels[0]=0;info->inChannels[1]=info->outChannels[1]=1;
        return std::make_unique<FilterConfiguration>(&engine,std::vector<FilterInfo*>{info},2);
    };
    auto oldGraph=makeGraph(0),newGraph=makeGraph(257);
    double input[2][480],output[2][480];double* ip[]={input[0],input[1]},*op[]={output[0],output[1]};
    unsigned transition=0;std::uint64_t position=0;
    for(unsigned block=0;block<100;++block) {
        for(unsigned f=0;f<480;++f)input[0][f]=input[1][f]=std::sin((position+f)*0.013);
        oldGraph->read(ip,480);oldGraph->process(480);newGraph->read(ip,480);newGraph->process(480);
        transition=oldGraph->doTransition(newGraph.get(),480,transition,4800);oldGraph->write(op,480);
        if(block>=5)for(unsigned f=0;f<480;++f)check(std::abs(output[0][f]-std::sin((position+f-257)*0.013))<1e-12,"graph transition must align actual outputs");
        position+=480;
    }
    check(transition>=4800,"graph transition must finish after warmup");
    DeleteFileW(path.c_str());std::cout<<"PASS actual FilterConfiguration crossfade / 257-sample alignment / warmup\n";
}
static void builtinTransportTest() {
    for(unsigned mode=0;mode<2;++mode) {
        std::unique_ptr<IFilter> filter;
        if(mode==0)filter=std::make_unique<OutProcGainFilter>(20*std::log10(0.5));
        else filter=std::make_unique<OutProcBiquadFilter>(BiQuad::PEAKING,3,1000,0.707,false,false);
        filter->initialize(48000,480,{L"L",L"R"});Sleep(250);
        double input[2][480],output[2][480]{};double* ip[]={input[0],input[1]},*op[]={output[0],output[1]};
        std::uint64_t position=0,checks=0,failures=0;double maxError=0;
        BiQuad reference(BiQuad::PEAKING,3,1000,48000,0.707,false);std::vector<double> expectedHistory(96000);
        for(unsigned block=0;block<200;++block) {
            for(unsigned f=0;f<480;++f) {input[0][f]=input[1][f]=0.1*std::sin((position+f)*0.013);expectedHistory[position+f]=mode==0?0.5*input[0][f]:reference.process(input[0][f]);}
            filter->process(op,ip,480);
            if(block>=10)for(unsigned f=0;f<480;++f) {
                const double expected=expectedHistory[position+f-960];
                maxError=(std::max)(maxError,std::abs(output[0][f]-expected));
                ++checks;if(std::abs(output[0][f]-expected)>1e-9 || !std::isfinite(output[1][f]))++failures;
            }
            position+=480;Sleep(10);
        }
        std::cout<<"builtin mode="<<mode<<" checks="<<checks<<" failures="<<failures<<" max_error="<<maxError<<"\n";
        check(checks && !failures,"actual builtin async host gain/biquad oracle");
        std::cout<<"PASS builtin async "<<(mode==0?"gain":"biquad")<<" samples="<<checks<<" latency="<<filter->getLatencySamples()<<"\n";
    }
}
int wmain(int argc,wchar_t** argv) {
    try {
        ringTest();overflowTest();hostMessageTest();panTest();meterTest();graphTransitionTest();
        if(argc>2 && std::wstring(argv[2])==L"--builtin")builtinTransportTest();
        if(argc>1) {
            auto library=VSTPluginLibrary::getInstance(argv[1]);check(library->initialize()>=0,"fixture load");
            VSTPluginInstance instance(library,4);check(instance.initialize(),"fixture initialize");
            std::wstring state;std::unordered_map<std::wstring,float> parameters;instance.readFromEffect(state,parameters);
            auto edited=parameters;edited[L"#7"]=0.25f;instance.writeToEffect(state,edited);
            std::wstring roundtrip;std::unordered_map<std::wstring,float> restored;instance.readFromEffect(roundtrip,restored);
            check(restored[L"#7"]==0.25f,"explicit parameters override saved envelope");
            check(restored.find(L"Gain")==restored.end(),"VST3 identity must use ParamID");
            std::cout<<"PASS fixture state overrides and ParamID identity\n";
            auto invalid=std::make_unique<VSTPluginInstance>(library,4,-1);
            check(!invalid->initialize(),"invalid ClassUID/index must not load class zero");
            instance.setUsedChannelCount(2);instance.prepareForProcessing(48000,480,true);instance.startProcessing();
            double input[2][480],output[2][480]{};double* ip[]={input[0],input[1]},*op[]={output[0],output[1]};
            for(auto& channel:input)for(auto& sample:channel)sample=1;
            instance.queueVST3ParameterChange(7,1,0);instance.processDoubleReplacing(ip,op,480);
            instance.queueVST3ParameterChange(7,0.25,17);instance.processDoubleReplacing(ip,op,480);
            for(unsigned f=0;f<480;++f)check(output[0][f]==(f<17?1:0.25),"sample-accurate automation offset");
            instance.queueVST3ParameterChange(7,0.5,99999);instance.processDoubleReplacing(ip,op,480);
            check(output[0][478]==0.25 && output[0][479]==0.5,"offset above block must clamp to final sample");
            instance.queueVST3ParameterChange(7,0.75,-20);instance.processDoubleReplacing(ip,op,480);
            check(output[0][0]==0.75,"negative offset must clamp to zero");
            instance.stopProcessing();std::cout<<"PASS strict class index and sample-accurate bounded automation\n";
            instance.writeToEffect(L"invalid base64 !",{});
            check(!instance.wasStateRestored(),"malformed state must report rejection");
            instance.writeToEffect(state,parameters);
            check(instance.wasStateRestored(),"valid state must recover after rejected state");
            std::cout<<"PASS explicit state restoration status\n";
            SetEnvironmentVariableW(L"EQAPO_FIXTURE_FIXED_DRAG_SIZE",L"1");
            HWND parent=CreateWindowExW(0,L"STATIC",L"EqAPO owned resize fixture",WS_OVERLAPPEDWINDOW,0,0,900,600,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
            check(parent!=nullptr,"owned native parent");
            short initialWidth=0,initialHeight=0;
            check(instance.startEditing(parent,&initialWidth,&initialHeight),"fixture native view attach");
            for(unsigned cycle=0;cycle<80;++cycle)for(float scale:{1.f,1.25f,1.5f,2.f,1.f}) {
                instance.setEditorScale(scale);int width=0,height=0;
                check(instance.getEditorSize(&width,&height),"native view size query");
                check(width==int(400*scale) && height==int(200*scale),"plugin scale size must survive fixed host-drag constraints");
                check(IsWindow(parent),"parent must remain alive while view attached");
            }
            instance.stopEditing();check(IsWindow(parent),"detach must preserve parent lifetime");DestroyWindow(parent);
            SetEnvironmentVariableW(L"EQAPO_FIXTURE_FIXED_DRAG_SIZE",nullptr);
            std::cout<<"PASS 400 native plugin scale requests / exact size / no cumulative growth / fixed host-drag constraints\n";
        }
        if(argc>3) {
            auto library=VSTPluginLibrary::getInstance(argv[3]);check(library->initialize()>=0,"VST2 fixture load");
            VSTPluginInstance instance(library,4);check(instance.initialize(),"VST2 fixture initialize");
            std::wstring state;std::unordered_map<std::wstring,float> parameters;instance.readFromEffect(state,parameters);
            check(parameters.count(L"#0")==1 && parameters.count(L"Gain")==0,"VST2 persistence uses index identity");
            instance.writeToEffect(state,{{L"Gain",.25f}});instance.readFromEffect(state,parameters);
            check(parameters.at(L"#0")==.25f,"legacy name overrides saved canonical state");
            instance.writeToEffect(state,{{L"#0",.5f},{L"Gain",.75f}});instance.readFromEffect(state,parameters);
            check(parameters.at(L"#0")==.5f,"canonical identity wins conflicting aliases");
            instance.writeToEffect(state,{{L"#0",2.f}});instance.readFromEffect(state,parameters);
            check(parameters.at(L"#0")==.5f,"invalid normalized VST2 edit rejected");
            std::cout<<"PASS VST2 canonical persistence / legacy restoration / alias precedence / normalized bounds\n";
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
