#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cwchar>
using namespace Steinberg;
using namespace Steinberg::Vst;
static const FUID processorID(0xEAA00001,0x00000001,0x10000001,0x90000001);
static const FUID controllerID(0xEAA00001,0x00000002,0x10000001,0x90000001);

class FixtureProcessor final:public AudioEffect {
public:
    FixtureProcessor() {setControllerClass(controllerID);}
    ~FixtureProcessor() override {if(handler)handler->release();}
    static FUnknown* createInstance(void*) {return static_cast<IAudioProcessor*>(new FixtureProcessor);}
    tresult PLUGIN_API initialize(FUnknown* context) override {
        if(AudioEffect::initialize(context)!=kResultOk) return kResultFalse;
        char delay[16]{};GetEnvironmentVariableA("EQAPO_FIXTURE_INITIALIZE_DELAY_MS",delay,sizeof(delay));
        const auto milliseconds=std::strtoul(delay,nullptr,10);
        if(milliseconds && milliseconds<=30000)Sleep(DWORD(milliseconds)); // Test-only SDK loading delay, never process().
        char mode[32]{};GetEnvironmentVariableA("EQAPO_FIXTURE_FAULT",mode,sizeof(mode));
        hangs=std::strcmp(mode,"hang")==0;crashes=std::strcmp(mode,"crash")==0;
        latencyChanges=std::strcmp(mode,"latency")==0;reloads=std::strcmp(mode,"reload")==0;
        float32Only=std::strcmp(mode,"float32")==0;
        float64Only=std::strcmp(mode,"float64")==0;
        context->queryInterface(IComponentHandler::iid,reinterpret_cast<void**>(&handler));
        addAudioInput(STR16("Main"),SpeakerArr::kStereo);
        addAudioInput(STR16("Sidechain (unrouted)"),SpeakerArr::kStereo,kAux,0);
        addAudioOutput(STR16("Main"),SpeakerArr::kStereo);return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override {return (size==kSample32 && !float64Only) || (size==kSample64 && !float32Only) ? kResultOk : kResultFalse;}
    uint32 PLUGIN_API getLatencySamples() override {return latency;}
    tresult PLUGIN_API setActive(TBool active) override {if(active) {history={};position=0;}return AudioEffect::setActive(active);}
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in,int32 ni,SpeakerArrangement* out,int32 no) override {
        if(ni!=2 || no!=1 || SpeakerArr::getChannelCount(in[0])>8 || in[0]!=out[0]) return kResultFalse;
        getAudioInput(0)->setArrangement(in[0]);getAudioInput(1)->setArrangement(in[1]);getAudioOutput(0)->setArrangement(out[0]);return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* stream) override {
        const uint32 marker=0x434F4D50;stream->write((void*)&marker,sizeof(marker));return stream->write(&gain,sizeof(gain));
    }
    tresult PLUGIN_API setState(IBStream* stream) override {
        uint32 marker=0;double value=0;
        if(stream->read(&marker,sizeof(marker))!=kResultOk || marker!=0x434F4D50 || stream->read(&value,sizeof(value))!=kResultOk) return kResultFalse;
        gain=value;return kResultOk;
    }
    tresult PLUGIN_API process(ProcessData& data) override {
        // Make precision regressions fail on the actual SDK process call,
        // rather than trusting only the capability query made at startup.
        if(data.symbolicSampleSize!=kSample32 && data.symbolicSampleSize!=kSample64)return kResultFalse;
        if(float32Only && data.symbolicSampleSize!=kSample32)return kResultFalse;
        if(float64Only && data.symbolicSampleSize!=kSample64)return kResultFalse;
        if(!data.numSamples) return kResultOk;
        if(!data.processContext || !(data.processContext->state&ProcessContext::kPlaying))return kResultFalse;
        if(position>=24000 && !restarted && handler && (latencyChanges || reloads)) {
            restarted=true;
            if(latencyChanges)latency=129;
            handler->restartComponent(latencyChanges ? kLatencyChanged : kReloadComponent);
        }
        if(position>=48000) {
            if(hangs)Sleep(INFINITE);
            if(crashes)RaiseFailFastException(nullptr,nullptr,0);
        }
        if(data.numInputs!=2 || data.numOutputs!=1 || data.inputs[1].numChannels!=0) return kResultFalse;
        double meterSquared=0;
        for(int32 f=0;f<data.numSamples;++f,++position) {
            if(data.inputParameterChanges) for(int32 q=0;q<data.inputParameterChanges->getParameterCount();++q) {
                auto* queue=data.inputParameterChanges->getParameterData(q);
                for(int32 p=0;p<queue->getPointCount();++p) {int32 offset=0;double value=0;queue->getPoint(p,offset,value);if(offset==f && queue->getParameterId()==7) gain=value;}
            }
            for(int32 c=0;c<data.outputs[0].numChannels;++c) {
                if(c>=8) return kResultFalse;
                const double sample=data.symbolicSampleSize==kSample64 ? data.inputs[0].channelBuffers64[c][f] : data.inputs[0].channelBuffers32[c][f];
                history[c][position%258]=sample;
                const double result=position>=latency ? gain*history[c][(position-latency)%258] : 0;
                if(c==0)meterSquared+=result*result;
                if(data.symbolicSampleSize==kSample64) data.outputs[0].channelBuffers64[c][f]=result;
                else data.outputs[0].channelBuffers32[c][f]=float(result);
            }
        }
        if(data.outputParameterChanges) {int32 qi=-1,pi=-1;auto* q=data.outputParameterChanges->addParameterData(8,qi);if(q) q->addPoint(data.numSamples-1,gain,pi);}
        if(data.outputParameterChanges) {int32 qi=-1,pi=-1;auto* q=data.outputParameterChanges->addParameterData(9,qi);if(q)q->addPoint(data.numSamples-1,std::sqrt(meterSquared/data.numSamples),pi);}
        return kResultOk;
    }
private:
    std::array<std::array<double,258>,8> history{};
    std::uint64_t position=0;double gain=1;bool hangs=false,crashes=false;
    uint32 latency=257;bool latencyChanges=false,reloads=false,float32Only=false,float64Only=false,restarted=false;
    IComponentHandler* handler=nullptr;
};
class FixtureView final:public IPlugView,public IPlugViewContentScaleSupport {
    EditController* controller=nullptr;
    static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l) {
        auto* self=reinterpret_cast<FixtureView*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<FixtureView*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(message==WM_LBUTTONUP && self){
            IComponentHandler2* group=nullptr;auto* componentHandler=self->controller->getComponentHandler();
            if(componentHandler && componentHandler->queryInterface(IComponentHandler2::iid,reinterpret_cast<void**>(&group))==kResultOk)group->startGroupEdit();
            const double value=self->controller->getParamNormalized(7)==1 ? .25 : 1;
            self->controller->setParamNormalized(7,value);self->controller->beginEdit(7);
            char mode[32]{};GetEnvironmentVariableA("EQAPO_FIXTURE_GUI_EDIT",mode,sizeof(mode));
            if(!std::strcmp(mode,"dirty") || !std::strcmp(mode,"restart")) {
                auto* handler=self->controller->getComponentHandler();
                if(handler && !std::strcmp(mode,"restart"))handler->restartComponent(kParamValuesChanged);
                if(handler && !std::strcmp(mode,"dirty")) {
                    IComponentHandler2* extended=nullptr;
                    if(handler->queryInterface(IComponentHandler2::iid,reinterpret_cast<void**>(&extended))==kResultOk){extended->setDirty(true);extended->release();}
                }
                self->controller->endEdit(7);if(group){group->finishGroupEdit();group->release();}return 0;
            }
            self->controller->performEdit(7,value);self->controller->endEdit(7);if(group){group->finishGroupEdit();group->release();}return 0;
        }
        if(message==WM_TIMER && self){wchar_t caption[128]{},previous[128]{};swprintf_s(caption,L"Fixture actual output RMS %.6f",self->controller->getParamNormalized(9));GetWindowTextW(window,previous,128);if(std::wcscmp(caption,previous)){SetWindowTextW(window,caption);InvalidateRect(window,nullptr,TRUE);}return 0;}
        if(message==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT rect{};GetClientRect(window,&rect);FillRect(dc,&rect,GetSysColorBrush(COLOR_WINDOW));wchar_t text[128]{};GetWindowTextW(window,text,128);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);EndPaint(window,&paint);return 0;}
        return DefWindowProcW(window,message,w,l);
    }
public:
    explicit FixtureView(EditController* value):controller(value){controller->addRef();}
    ~FixtureView(){controller->release();}
    tresult PLUGIN_API queryInterface(const TUID id,void** out) override {
        if(!out) return kInvalidArgument;
        if(FUnknownPrivate::iidEqual(id,IPlugView::iid)||FUnknownPrivate::iidEqual(id,FUnknown::iid)) *out=static_cast<IPlugView*>(this);
        else if(FUnknownPrivate::iidEqual(id,IPlugViewContentScaleSupport::iid)) *out=static_cast<IPlugViewContentScaleSupport*>(this);
        else {*out=nullptr;return kNoInterface;}addRef();return kResultOk;
    }
    uint32 PLUGIN_API addRef() override {return ++refs;}
    uint32 PLUGIN_API release() override {auto n=--refs;if(!n) delete this;return n;}
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override {return type && !std::strcmp(type,kPlatformTypeHWND) ? kResultTrue : kResultFalse;}
    tresult PLUGIN_API attached(void* parent,FIDString type) override {
        if(isPlatformTypeSupported(type)!=kResultTrue) return kResultFalse;
        WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName=L"EqApoFixtureMeter";RegisterClassW(&wc);
        child=CreateWindowExW(0,wc.lpszClassName,L"EqAPO fixture: 257 samples / distinct state / resize / DPI",WS_CHILD|WS_VISIBLE,0,0,size.getWidth(),size.getHeight(),static_cast<HWND>(parent),nullptr,GetModuleHandle(nullptr),this);
        if(child)SetTimer(child,1,50,nullptr);
        return child ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API removed() override {if(child) DestroyWindow(child);child=nullptr;return kResultOk;}
    tresult PLUGIN_API onWheel(float) override {return kResultFalse;}
    tresult PLUGIN_API onKeyDown(char16,int16,int16) override {return kResultFalse;}
    tresult PLUGIN_API onKeyUp(char16,int16,int16) override {return kResultFalse;}
    tresult PLUGIN_API getSize(ViewRect* rect) override {if(!rect)return kInvalidArgument;*rect=size;return kResultOk;}
    tresult PLUGIN_API onSize(ViewRect* rect) override {if(!rect)return kInvalidArgument;size=*rect;if(child) SetWindowPos(child,nullptr,0,0,size.getWidth(),size.getHeight(),SWP_NOZORDER);return kResultOk;}
    tresult PLUGIN_API onFocus(TBool) override {return kResultOk;}
    tresult PLUGIN_API setFrame(IPlugFrame* value) override {frame=value;return kResultOk;}
    tresult PLUGIN_API canResize() override {return kResultTrue;}
    tresult PLUGIN_API checkSizeConstraint(ViewRect* rect) override {
        if(!rect)return kInvalidArgument;
        // Fixed host-drag constraints must not override a plugin scale request.
        char fixed[2]{};GetEnvironmentVariableA("EQAPO_FIXTURE_FIXED_DRAG_SIZE",fixed,sizeof(fixed));
        if(fixed[0]=='1') {*rect=size;return kResultOk;}
        rect->right=rect->left+(std::max)(200,rect->getWidth());rect->bottom=rect->top+(std::max)(100,rect->getHeight());return kResultOk;
    }
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override {if(factor<=0)return kInvalidArgument;ViewRect next(0,0,int32(400*factor),int32(200*factor));if(frame) frame->resizeView(this,&next);else size=next;return kResultOk;}
private:
    std::atomic<uint32> refs{1};HWND child=nullptr;ViewRect size{0,0,400,200};IPlugFrame* frame=nullptr;
};
class FixtureController final:public EditController {
public:
    static FUnknown* createInstance(void*) {return static_cast<IEditController*>(new FixtureController);}
    tresult PLUGIN_API initialize(FUnknown* context) override {
        if(EditController::initialize(context)!=kResultOk)return kResultFalse;
        parameters.addParameter(STR16("Gain"),nullptr,0,1,ParameterInfo::kCanAutomate,7);
        parameters.addParameter(STR16("Output gain"),nullptr,0,1,ParameterInfo::kIsReadOnly,8);
        parameters.addParameter(STR16("Output RMS"),nullptr,0,0,ParameterInfo::kIsReadOnly,9);return kResultOk;
    }
    tresult PLUGIN_API setComponentState(IBStream* stream) override {uint32 marker=0;double gain=0;if(stream->read(&marker,4)!=kResultOk || marker!=0x434F4D50 || stream->read(&gain,8)!=kResultOk)return kResultFalse;setParamNormalized(7,gain);return kResultOk;}
    tresult PLUGIN_API getState(IBStream* stream) override {uint32 marker=0x4354524C;stream->write(&marker,4);return stream->write(&privateState,4);}
    tresult PLUGIN_API setState(IBStream* stream) override {uint32 marker=0;if(stream->read(&marker,4)!=kResultOk || marker!=0x4354524C)return kResultFalse;return stream->read(&privateState,4);}
    IPlugView* PLUGIN_API createView(FIDString name) override {return name && !std::strcmp(name,ViewType::kEditor) ? new FixtureView(this) : nullptr;}
private: uint32 privateState=0x11223344;
};
BEGIN_FACTORY_DEF("EqAPO engineering fixtures","","fixture@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(processorID),PClassInfo::kManyInstances,kVstAudioEffectClass,"EqAPO 257 Sample Fixture",Vst::kDistributable,"Fx","1.0.0",kVstVersionString,FixtureProcessor::createInstance)
DEF_CLASS2(INLINE_UID_FROM_FUID(controllerID),PClassInfo::kManyInstances,kVstComponentControllerClass,"EqAPO Fixture Controller",0,"","1.0.0",kVstVersionString,FixtureController::createInstance)
END_FACTORY
