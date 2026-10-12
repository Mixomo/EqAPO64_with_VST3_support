#include "helpers/aeffectx.h"
#include <windows.h>
#include <cstring>
#include <atomic>
struct Fixture {vst_effect_t effect{};float gain=1;vst_host_callback_t host=nullptr;HWND editor=nullptr;bool hadEditor=false;};
static void lifecycleMarker(const char* text) {
    wchar_t path[32768]{};const DWORD length=GetEnvironmentVariableW(L"EQAPO_FIXTURE_GUI_CLOSE_MARKER",path,32768);
    if(!length || length>=32768)return;
    HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){DWORD bytes=0;WriteFile(file,text,DWORD(std::strlen(text)),&bytes,nullptr);CloseHandle(file);}
}
static Fixture* self(vst_effect_t* effect) {return static_cast<Fixture*>(effect->effect_internal);}
static LRESULT CALLBACK fixtureEditor(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_NCCREATE) SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    auto* f=reinterpret_cast<Fixture*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_LBUTTONUP && f) {
        f->gain=f->gain==1.0f ? 0.25f : 1.0f;
        f->host(&f->effect,VST_HOST_OPCODE_AUTOMATE,0,0,nullptr,f->gain);
        SetWindowTextW(window,f->gain==1.0f ? L"Fixture gain 1.0" : L"Fixture gain 0.25");
        return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
static intptr_t VST_FUNCTION_INTERFACE control(vst_effect_t* effect,int32_t opcode,int32_t index,intptr_t value,void* ptr,float opt) {
    auto* f=self(effect);
    switch(opcode) {
    case 1: if(f->editor) DestroyWindow(f->editor);if(f->hadEditor)lifecycleMarker("INSTANCE_DESTROYED\n");delete f;return 1;
    case VST_EFFECT_OPCODE_PARAM_GET_NAME: if(ptr) std::strcpy(static_cast<char*>(ptr),"Gain");return 1;
    case VST_EFFECT_OPCODE_GET_CHUNK_DATA: if(ptr) *static_cast<void**>(ptr)=&f->gain;return sizeof(f->gain);
    case VST_EFFECT_OPCODE_SET_CHUNK_DATA: if(ptr && value==sizeof(f->gain)) std::memcpy(&f->gain,ptr,sizeof(f->gain));return 1;
    case 45: if(ptr) std::strcpy(static_cast<char*>(ptr),"EqAPO VST2 fixture");return 1;
    case 13: {static vst_rect_t rect{0,0,200,400};if(ptr) *static_cast<vst_rect_t**>(ptr)=&rect;return 1;}
    case 14: {
        WNDCLASSW wc{};wc.lpfnWndProc=fixtureEditor;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"EqApoFixtureControl";RegisterClassW(&wc);
        f->editor=CreateWindowExW(0,wc.lpszClassName,L"Fixture gain 1.0",WS_CHILD|WS_VISIBLE,0,0,400,200,static_cast<HWND>(ptr),nullptr,wc.hInstance,f);f->hadEditor=f->editor!=nullptr;return f->editor!=nullptr;
    }
    case 15: {
        if(f->editor) {
            wchar_t fault[8]{};GetEnvironmentVariableW(L"EQAPO_FIXTURE_GUI_CLOSE_HANG",fault,8);
            if(fault[0]==L'1'){lifecycleMarker("VIEW_CLOSE_HANG\n");Sleep(INFINITE);}
            lifecycleMarker("VIEW_DETACHED\n");DestroyWindow(f->editor);
        }
        f->editor=nullptr;return 1;
    }
    default:return 0;
    }
}
static void VST_FUNCTION_INTERFACE setParam(vst_effect_t* effect,uint32_t,float value) {self(effect)->gain=value;}
static float VST_FUNCTION_INTERFACE getParam(vst_effect_t* effect,uint32_t) {return self(effect)->gain;}
static void VST_FUNCTION_INTERFACE processFloat(vst_effect_t* effect,const float* const* input,float** output,int32_t frames) {for(int c=0;c<2;++c)for(int f=0;f<frames;++f)output[c][f]=input[c][f]*self(effect)->gain;}
static void VST_FUNCTION_INTERFACE processDouble(vst_effect_t* effect,const double* const* input,double** output,int32_t frames) {for(int c=0;c<2;++c)for(int f=0;f<frames;++f)output[c][f]=input[c][f]*self(effect)->gain;}
extern "C" __declspec(dllexport) vst_effect_t* VST_FUNCTION_INTERFACE VSTPluginMain(vst_host_callback_t callback) {
    auto* f=new Fixture;auto& e=f->effect;f->host=callback;
    e.magic_number=VST_FOURCC('V','s','t','P');e.control=control;e.set_parameter=setParam;e.get_parameter=getParam;
    e.num_inputs=e.num_outputs=2;e.num_params=1;e.num_programs=1;
    e.flags=VST_EFFECT_FLAG_SUPPORTS_FLOAT|VST_EFFECT_FLAG_SUPPORTS_DOUBLE|VST_EFFECT_FLAG_CHUNKS|VST_EFFECT_FLAG_EDITOR;
    e.unique_id=VST_FOURCC('E','T','S','2');e.version=1;e.effect_internal=f;e.process_float=processFloat;e.process_double=processDouble;return &e;
}
