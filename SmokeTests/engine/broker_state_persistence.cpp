#include "stdafx.h"
#include "helpers/VSTPluginLibrary.h"
#include "helpers/VSTPluginInstance.h"
#include "outproc/BrokerInstanceChannel.h"
#include "outproc/AsyncAudioClient.h"
#include <memory>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include "helpers/StringHelper.h"

static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static HWND windowFor(unsigned pid){
    struct Search{unsigned pid;HWND result=nullptr;} search{pid};
    EnumWindows([](HWND w,LPARAM raw)->BOOL{auto& s=*reinterpret_cast<Search*>(raw);DWORD pid=0;GetWindowThreadProcessId(w,&pid);wchar_t title[128]{};GetWindowTextW(w,title,128);if(pid==s.pid && std::wstring(title)==L"Equalizer APO - VST")s.result=w;return TRUE;},reinterpret_cast<LPARAM>(&search));return search.result;
}
struct Node{
    OutProcVSTConfig preset;
    std::wstring session,instance;
    std::uint64_t identity=0,checkpoint=0;
    unsigned parameter=0,pid=0;double edited=.25;
    std::unique_ptr<EqApoControl::ControlEndpointOwner> control;
    std::unique_ptr<EqApoBroker::Owner> audio;
    EqApoAsync::AudioClient client;
    HANDLE process=nullptr;HWND window=nullptr;
    ~Node(){if(audio)SetEvent(audio->shutdown);if(process){if(WaitForSingleObject(process,8000)!=WAIT_OBJECT_0){TerminateProcess(process,99);WaitForSingleObject(process,2000);}CloseHandle(process);}}
};
int wmain(int argc,wchar_t** argv){
    if(argc!=7)return 2;
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);struct Apartment{~Apartment(){CoUninitialize();}} apartment;
    try{
        const auto sid=EqApoControl::currentUserSid();std::vector<std::unique_ptr<Node>> nodes;
        for(unsigned i=0;i<3;++i){
            auto n=std::make_unique<Node>();n->preset.libraryPath=argv[i+2];n->parameter=i==0?7:i==1?0:std::stoul(argv[5]);n->edited=i==2?std::stod(argv[6]):.25;
            auto library=VSTPluginLibrary::getInstance(n->preset.libraryPath);check(library->initialize()>=0,"load test plugin");
            auto effect=std::make_unique<VSTPluginInstance>(library,2);check(effect->initialize(),"initialize test plugin");effect->readFromEffect(n->preset.chunkData,n->preset.paramMap);
            if(library->isVST3())n->preset.classUID=library->getVST3ClassUID(0);effect.reset();
            n->session=L"broker-state-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(i);n->instance=n->session+L"-instance";
            n->identity=EqApoControl::controlIdentity(n->preset.libraryPath,n->preset.classUID,0);
            n->control=std::make_unique<EqApoControl::ControlEndpointOwner>();check(n->control->create(n->session,sid,n->identity),"create own control endpoint");
            n->audio=std::make_unique<EqApoBroker::Owner>();check(n->audio->create(n->session+L"-first",sid,n->preset,48000,6,480,n->session,n->control->getGeneration(),n->instance),"create own audio session");
            std::wstring command=L"\""+std::wstring(argv[1])+L"\" --broker-session "+n->audio->id+L" --broker-space "+n->audio->space;
            STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};check(CreateProcessW(argv[1],command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE,"launch own candidate worker");CloseHandle(child.hThread);n->process=child.hProcess;n->pid=child.dwProcessId;
            n->client.prepare(6,480,48000);nodes.push_back(std::move(n));
        }
        unsigned long long position=0;bool finite=true;double energy=0;unsigned currentRate=48000,sourceNumber=0;
        wchar_t editorPath[32768]{};GetEnvironmentVariableW(L"EQAPO_STATE_TEST_EDITOR_SIGNAL",editorPath,32768);const std::wstring editorSignal=editorPath;
        auto signal=[&](const wchar_t* suffix){std::ofstream file(editorSignal+suffix);file<<"ready";check(file.good(),"write test signal");};
        auto pump=[&](unsigned blocks){for(unsigned b=0;b<blocks;++b){
            double signal[6][480]{},next[6][480]{};double* input[6];double* output[6];
            for(unsigned c=0;c<6;++c){input[c]=signal[c];output[c]=next[c];for(unsigned f=0;f<480;++f)signal[c][f]=c<2?.002*std::sin(6.283185307179586*(513+c*97+sourceNumber*113)*(position+f)/currentRate):0;}
            for(auto& n:nodes){EqApoControl::ParameterEvent edit;while(n->control->consume(edit))check(n->audio->header->parameters.publish(n->audio->header->parameters.generation,edit.id,edit.normalized,edit.sampleOffset),"relay GUI edit to DSP");n->client.process(output,input,480,n->audio->header);SetEvent(n->audio->request);for(unsigned c=0;c<6;++c)for(unsigned f=0;f<480;++f){finite &= std::isfinite(next[c][f]);signal[c][f]=next[c][f];}}
            for(auto& channel:signal)for(auto sample:channel)energy+=sample*sample;position+=480;Sleep(10);
        }};
        pump(200);
        auto waitSignal=[&](const wchar_t* suffix){for(unsigned n=0;n<1000 && GetFileAttributesW((editorSignal+suffix).c_str())==INVALID_FILE_ATTRIBUTES;++n)pump(1);check(GetFileAttributesW((editorSignal+suffix).c_str())!=INVALID_FILE_ATTRIBUTES,"external Editor driver timed out");};
        for(auto& n:nodes)check(n->audio->header->ready.load() && n->audio->metadata->workerPid.load()==n->pid,"candidate worker processes audio");
        if(!editorSignal.empty()){
            std::ofstream config(editorSignal+L".config.txt");
            for(auto& n:nodes){std::wstring line=L"VSTPlugin: Library \""+n->preset.libraryPath+L"\" HostId "+n->session+L" GuiUserSID "+sid+L" ClassIndex 0";if(!n->preset.classUID.empty())line+=L" ClassUID "+n->preset.classUID;line+=L" ChunkData \""+n->preset.chunkData+L"\"\n";config<<StringHelper::toString(line,CP_UTF8);}config.close();signal(L".ready");waitSignal(L".opened");
        }else for(auto& n:nodes)check(EqApoControl::nativeViewControl(n->session,n->identity,1).available,"request panel");
        pump(70);
        for(unsigned i=0;i<nodes.size();++i){auto& n=*nodes[i];n.window=windowFor(n.pid);check(n.window!=nullptr,"native panel exists");
            if(i<2){struct Search{const wchar_t* name;HWND found=nullptr;} search{i==0?L"EqApoFixtureMeter":L"EqApoFixtureControl"};EnumChildWindows(n.window,[](HWND w,LPARAM raw)->BOOL{auto& s=*reinterpret_cast<Search*>(raw);wchar_t name[128]{};GetClassNameW(w,name,128);if(std::wstring(name)==s.name)s.found=w;return TRUE;},reinterpret_cast<LPARAM>(&search));check(search.found!=nullptr,"actual fixture GUI control");PostMessageW(search.found,WM_LBUTTONUP,0,0);}
            else {
                wchar_t path[32768]{};
                if(GetEnvironmentVariableW(L"EQAPO_STATE_TEST_GUI_READY",path,32768)){
                    std::ofstream ready(path);ready<<"{\"pid\":"<<n.pid<<",\"hwnd\":"<<reinterpret_cast<std::uintptr_t>(n.window)<<"}";ready.close();
                    const std::wstring completed=std::wstring(path)+L".done";
                    for(unsigned wait=0;wait<1000 && GetFileAttributesW(completed.c_str())==INVALID_FILE_ATTRIBUTES;++wait)pump(1);
                    check(GetFileAttributesW(completed.c_str())!=INVALID_FILE_ATTRIBUTES,"external GUI edit was not acknowledged");
                } else check(EqApoControl::publishControl(n.session,n.identity,n.parameter,n.edited,0),"edit supplied plug-in parameter");
            }
        }
        pump(100);
        // An external GUI driver can request an oracle from the checkpoint
        // published while the edited panel is still open, before any lifecycle
        // transition. A later close/rebind must preserve this measured value.
        for(auto& n:nodes)if(n->edited<0){
            std::wstring path;check(n->control->readNativeSnapshot(n->checkpoint,path),"edited open-panel checkpoint");OutProcVSTConfig live;check(OutProcReadVSTConfig(path,live),"read open-panel state");
            const auto key=L"#"+std::to_wstring(n->parameter);check(live.paramMap.count(key) && n->preset.paramMap.count(key),"tracked GUI parameter");
            n->edited=live.paramMap.at(key);check(std::abs(n->edited-n->preset.paramMap.at(key))>1e-5,"GUI edit must change the starting value");
            std::cout<<"open-panel parameter="<<n->parameter<<" edited="<<n->edited<<std::endl;
        }
        auto checkpoint=[&](Node& n){std::wstring path;check(n.control->readNativeSnapshot(n.checkpoint,path),"new complete checkpoint");OutProcVSTConfig saved;check(OutProcReadVSTConfig(path,saved) && !saved.chunkData.empty(),"read full saved state");const auto found=saved.paramMap.find(L"#"+std::to_wstring(n.parameter));std::cout<<"checkpoint parameter="<<n.parameter<<" expected="<<n.edited<<" actual="<<(found==saved.paramMap.end()?-1:found->second)<<std::endl;check(found!=saved.paramMap.end() && std::abs(found->second-n.edited)<1e-5,"checkpoint preserves edited parameter");};
        if(!editorSignal.empty()){
            for(unsigned source=0;source<3;++source){
                ++sourceNumber;position=0;Sleep(100);pump(30);
                for(auto& n:nodes)check(n->audio->metadata->workerPid.load()==n->pid && windowFor(n->pid)==n->window,"source change retains worker and open GUI");
            }
            signal(L".close-request");waitSignal(L".closed");pump(30);
            for(auto& n:nodes){check(!windowFor(n->pid),"Editor exit leaves no orphan panel");checkpoint(*n);}
            signal(L".reopen-request");waitSignal(L".reopened");pump(30);
            for(auto& n:nodes)check(windowFor(n->pid)!=nullptr,"new Editor process reopens existing native panel");
            signal(L".final-close-request");waitSignal(L".final-closed");pump(30);
            std::vector<OutProcVSTConfig> saved;std::vector<std::wstring> sessions;std::vector<unsigned> parameters;std::vector<double> edits;
            std::ifstream config(editorSignal+L".config.txt");std::string line;
            while(std::getline(config,line)){
                auto text=StringHelper::toWString(line,CP_UTF8);if(text.rfind(L"VSTPlugin:",0)!=0)continue;
                auto tokens=StringHelper::splitQuoted(text.substr(11),L' ');OutProcVSTConfig preset;std::wstring session;
                for(unsigned j=0;j+1<tokens.size();j+=2){const auto& key=tokens[j];const auto& val=tokens[j+1];if(key==L"Library")preset.libraryPath=val;else if(key==L"ChunkData")preset.chunkData=val;else if(key==L"ClassUID")preset.classUID=val;else if(key==L"HostId")session=val;else if(key==L"ClassIndex")preset.vst3ClassIndex=std::stoi(val);}
                check(!preset.chunkData.empty(),"Editor saved complete VST state to disk");saved.push_back(std::move(preset));sessions.push_back(session);
            }
            check(saved.size()==nodes.size(),"all VST rows saved");for(auto& n:nodes){parameters.push_back(n->parameter);edits.push_back(n->edited);SetEvent(n->audio->shutdown);}nodes.clear();
            for(unsigned stream=0;stream<2;++stream){currentRate=stream?48000:44100;position=0;
                for(unsigned i=0;i<saved.size();++i){auto n=std::make_unique<Node>();n->preset=saved[i];n->session=sessions[i];n->instance=n->session+L"-stream-"+std::to_wstring(stream);n->parameter=parameters[i];n->edited=edits[i];n->identity=EqApoControl::controlIdentity(n->preset.libraryPath,n->preset.classUID,0);
                    n->control=std::make_unique<EqApoControl::ControlEndpointOwner>();check(n->control->create(n->session,sid,n->identity),"new stream control endpoint");n->audio=std::make_unique<EqApoBroker::Owner>();check(n->audio->create(n->instance,sid,n->preset,currentRate,6,480,n->session,n->control->getGeneration(),n->instance),"new stream from saved Editor state");
                    std::wstring cmd=L"\""+std::wstring(argv[1])+L"\" --broker-session "+n->audio->id+L" --broker-space "+n->audio->space;STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};check(CreateProcessW(argv[1],cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE,"launch new stream worker");CloseHandle(child.hThread);n->process=child.hProcess;n->pid=child.dwProcessId;n->client.prepare(6,480,currentRate);nodes.push_back(std::move(n));
                }
                pump(200);for(auto& n:nodes){check(n->audio->header->ready.load() && !windowFor(n->pid),"new stream starts ready with panels closed");EqApoControl::nativeViewControl(n->session,n->identity,1);}pump(40);
                for(auto& n:nodes)EqApoControl::nativeViewControl(n->session,n->identity,3);pump(40);for(auto& n:nodes){checkpoint(*n);SetEvent(n->audio->shutdown);}nodes.clear();
            }
            check(finite && energy>0,"Editor and new-stream PCM remains valid");std::cout<<"PASS source changes/gaps with open GUI, actual Editor close/reopen and disk checkpoints, no automatic panel reopening, new 44.1/48 kHz stream workers restore all edited parameters; isolated PCM only\n";return 0;
        }
        for(auto& n:nodes){std::cout<<"before close parameter="<<n->parameter<<" DSP sequence="<<n->audio->header->parameterSequence.load()<<" rejected="<<n->audio->header->parameterRejected.load()<<" error="<<n->audio->header->lastError.load()<<std::endl;EqApoControl::nativeViewControl(n->session,n->identity,3);}pump(50);
        for(auto& n:nodes){check(!windowFor(n->pid),"no orphan after close");checkpoint(*n);EqApoControl::nativeViewControl(n->session,n->identity,1);}pump(50);
        for(auto& n:nodes){n->window=windowFor(n->pid);check(n->window!=nullptr,"reopen native panel");EqApoControl::nativeViewControl(n->session,n->identity,2);}pump(20);
        for(unsigned i=0;i<nodes.size();++i){auto& n=*nodes[i];
            EqApoBroker::InstanceChannel route;check(route.open(n.instance,n.identity),"open own retained worker route");
            auto control=std::make_unique<EqApoControl::ControlEndpointOwner>();check(control->create(n.session,sid,n.identity,48000,6,false),"stage reactivation control");
            auto audio=std::make_unique<EqApoBroker::Owner>();check(audio->create(n.session+L"-reactivated",sid,n.preset,48000,6,480,n.session,control->getGeneration(),n.instance),"stage unchanged saved preset");
            check(route.submit(audio->id,audio->space,audio->metadata->generation,route.getGeneration()),"submit own handoff");SetEvent(n.audio->shutdown);
            n.audio=std::move(audio);n.client.prepare(6,480,48000);
            for(unsigned retry=0;retry<200 && !n.audio->header->ready.load();++retry)pump(1);
            check(n.audio->header->ready.load() && n.audio->metadata->workerPid.load()==n.pid,"same worker reactivated");control->activate();n.control=std::move(control);n.checkpoint=0;
            check(windowFor(n.pid)==n.window && !IsWindowVisible(n.window),"same hidden GUI retained across handoff");
            EqApoControl::nativeViewControl(n.session,n.identity,3);pump(40);checkpoint(n);
        }
        pump(100);check(finite && energy>0,"multi-plugin PCM remains finite and non-silent");
        for(auto& n:nodes)SetEvent(n->audio->shutdown);
        std::cout<<"PASS three candidate workers in a six-channel chain: native GUI edits, close/reopen, full checkpoints, same worker/hidden GUI after bypass, retained parameters, finite PCM; no installed audiodg\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
