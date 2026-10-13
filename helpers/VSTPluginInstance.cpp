/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include <wincrypt.h>
#include <inttypes.h>
#include <cstdint>
#include "StringHelper.h"
#include "LogHelper.h"
#include "../Version.h"
#include "VSTPluginLibrary.h"
#include "VSTPluginInstance.h"
#include "pluginterfaces/base/futils.h"
#include "VSTHostMessages.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstpluginterfacesupport.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
struct EditorInteractionTarget {HWND parent;VSTPluginInstance* instance;};
thread_local std::vector<EditorInteractionTarget> editorInteractionTargets;
thread_local HHOOK editorInteractionHook=nullptr;
LRESULT CALLBACK editorInputHook(int code,WPARAM removed,LPARAM data) {
    if(code>=0 && removed==PM_REMOVE && data) {
        const auto* message=reinterpret_cast<const MSG*>(data);
        const auto kind=message->message;
        if(kind==WM_LBUTTONUP || kind==WM_RBUTTONUP || kind==WM_KEYUP || kind==WM_MOUSEWHEEL
            || (kind==WM_MOUSEMOVE && (message->wParam&MK_LBUTTON)))
            for(const auto& target:editorInteractionTargets)
                if(message->hwnd==target.parent || IsChild(target.parent,message->hwnd))
                    target.instance->noteEditorInteraction();
    }
    // No SDK call, snapshot, allocation, wait or painting in this hook.
    return CallNextHookEx(editorInteractionHook,code,removed,data);
}
void watchEditorInput(HWND parent,VSTPluginInstance* instance) {
    editorInteractionTargets.push_back({parent,instance});
    if(!editorInteractionHook)editorInteractionHook=SetWindowsHookExW(WH_GETMESSAGE,editorInputHook,nullptr,GetCurrentThreadId());
}
void unwatchEditorInput(VSTPluginInstance* instance) {
    auto& targets=editorInteractionTargets;
    targets.erase(std::remove_if(targets.begin(),targets.end(),[instance](const EditorInteractionTarget& target){return target.instance==instance;}),targets.end());
    if(targets.empty() && editorInteractionHook){UnhookWindowsHookEx(editorInteractionHook);editorInteractionHook=nullptr;}
}
}

static thread_local bool insidePluginProcess = false;
struct PluginProcessScope {
    bool previous = insidePluginProcess;
    PluginProcessScope() noexcept { insidePluginProcess = true; }
    ~PluginProcessScope() { insidePluginProcess = previous; }
};

#define equalizerApoVSTID VST_FOURCC('E', 'A', 'P', 'O');



class VSTPluginInstance::VST3MemoryStream : public IBStream
{
public:
	VST3MemoryStream() {}
	VST3MemoryStream(const vector<char>& data) : data(data) {}

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		QUERY_INTERFACE(iid, obj, FUnknown::iid, IBStream)
		QUERY_INTERFACE(iid, obj, IBStream::iid, IBStream)
		*obj = NULL;
		return kNoInterface;
	}

	uint32 PLUGIN_API addRef() override { return InterlockedIncrement(&refCount); }
	uint32 PLUGIN_API release() override
	{
		uint32 result = InterlockedDecrement(&refCount);
		if (result == 0)
			delete this;
		return result;
	}

	tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead = nullptr) override
	{
		if(numBytes<0 || (!buffer && numBytes)) return kInvalidArgument;
		int32 available = (int32)min<size_t>(numBytes, data.size() - min<size_t>(position, data.size()));
		if (available > 0)
			memcpy(buffer, data.data() + position, available);
		position += available;
		if (numBytesRead != NULL)
			*numBytesRead = available;
		return kResultOk;
	}

	tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten = nullptr) override
	{
		if(numBytes<0 || (!buffer && numBytes) || size_t(numBytes)>64*1024*1024 || position>64*1024*1024-size_t(numBytes)) return kInvalidArgument;
		if (numBytes == 0)
		{
			if (numBytesWritten != NULL)
				*numBytesWritten = 0;
			return kResultOk;
		}
		if (position + numBytes > data.size())
			data.resize(position + numBytes);
		memcpy(data.data() + position, buffer, numBytes);
		position += numBytes;
		if (numBytesWritten != NULL)
			*numBytesWritten = numBytes;
		return kResultOk;
	}

	tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result = nullptr) override
	{
		int64 newPosition = 0;
		if (mode == kIBSeekSet)
			newPosition = pos;
		else if (mode == kIBSeekCur)
			newPosition = (int64)position + pos;
		else if (mode == kIBSeekEnd)
			newPosition = (int64)data.size() + pos;
        else return kInvalidArgument;
        if(newPosition<0 || newPosition>64*1024*1024) return kInvalidArgument;
		if (newPosition < 0)
			newPosition = 0;
		position = (size_t)newPosition;
		if (position > data.size())
			data.resize(position);
		if (result != NULL)
			*result = (int64)position;
		return kResultOk;
	}

	tresult PLUGIN_API tell(int64* pos) override
	{
		if (pos == NULL)
			return kInvalidArgument;
		*pos = (int64)position;
		return kResultOk;
	}

	const vector<char>& getData() const { return data; }

private:
	volatile LONG refCount = 1;
	vector<char> data;
	size_t position = 0;
};

class EmptyVST3ParameterChanges : public IParameterChanges
{
public:
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		QUERY_INTERFACE(iid, obj, FUnknown::iid, IParameterChanges)
		QUERY_INTERFACE(iid, obj, IParameterChanges::iid, IParameterChanges)
		*obj = NULL;
		return kNoInterface;
	}

	uint32 PLUGIN_API addRef() override { return 2; }
	uint32 PLUGIN_API release() override { return 1; }
	int32 PLUGIN_API getParameterCount() override { return 0; }
	IParamValueQueue* PLUGIN_API getParameterData(int32) override { return NULL; }
	IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32& index) override
	{
		index = -1;
		return NULL;
	}
};

class EmptyVST3EventList : public IEventList
{
public:
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		QUERY_INTERFACE(iid, obj, FUnknown::iid, IEventList)
		QUERY_INTERFACE(iid, obj, IEventList::iid, IEventList)
		*obj = NULL;
		return kNoInterface;
	}

	uint32 PLUGIN_API addRef() override { return 2; }
	uint32 PLUGIN_API release() override { return 1; }
	int32 PLUGIN_API getEventCount() override { return 0; }
	tresult PLUGIN_API getEvent(int32, Event&) override { return kInvalidArgument; }
	tresult PLUGIN_API addEvent(Event&) override { return kResultFalse; }
};

static EmptyVST3ParameterChanges emptyVST3ParameterChanges;
static EmptyVST3EventList emptyVST3EventList;

static const char vst2StateEnvelopeMagic[] = "EAPOVST2STATE2\n";
static const char vst3StateEnvelopeMagic[] = "EAPOVST3STATE3\n";
static const char vst3LegacyStateEnvelopeMagic[] = "EAPOVST3STATE2\n";
static const wchar_t vst3ParamIdPrefix[] = L"#";
static bool parseParameterId(const wstring& text,ParamID& id) {
    if(text.size()<2 || text[0]!=L'#')return false;
    uint64 value=0;
    for(size_t i=1;i<text.size();++i) {
        if(text[i]<L'0'||text[i]>L'9')return false;
        value=value*10+unsigned(text[i]-L'0');
        if(value>UINT32_MAX)return false;
    }
    id=ParamID(value);return true;
}

static bool base64Decode(const wstring& value, vector<char>& data)
{
	data.clear();
	if (value.empty())
		return false;

	DWORD bufSize = 0;
	if (CryptStringToBinaryW(value.c_str(), 0, CRYPT_STRING_BASE64, NULL, &bufSize, NULL, NULL) != TRUE || bufSize == 0)
		return false;

	data.assign(bufSize, 0);
	if (CryptStringToBinaryW(value.c_str(), 0, CRYPT_STRING_BASE64, (BYTE*)data.data(), &bufSize, NULL, NULL) != TRUE)
	{
		data.clear();
		return false;
	}
	data.resize(bufSize);
	return true;
}

static bool base64Encode(const vector<char>& data, wstring& value)
{
	value.clear();
	if (data.empty())
		return false;

	DWORD stringLength = 0;
	if (CryptBinaryToStringW((BYTE*)data.data(), (DWORD)data.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &stringLength) != TRUE || stringLength == 0)
		return false;

	vector<wchar_t> string(stringLength);
	if (CryptBinaryToStringW((BYTE*)data.data(), (DWORD)data.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, string.data(), &stringLength) != TRUE)
		return false;

	value = string.data();
	return true;
}

static void appendBytes(vector<char>& data, const void* value, size_t size)
{
	const char* begin = static_cast<const char*>(value);
	data.insert(data.end(), begin, begin + size);
}

static void appendUInt32(vector<char>& data, uint32_t value)
{
	appendBytes(data, &value, sizeof(value));
}

static bool readUInt32(const vector<char>& data, size_t& offset, uint32_t& value)
{
	if (offset + sizeof(value) > data.size())
		return false;
	memcpy(&value, data.data() + offset, sizeof(value));
	offset += sizeof(value);
	return true;
}

static bool readBytes(const vector<char>& data, size_t& offset, uint32_t size, vector<char>& value)
{
	if (offset + size > data.size())
		return false;
	value.assign(data.begin() + offset, data.begin() + offset + size);
	offset += size;
	return true;
}

static void appendBlob(vector<char>& data, const vector<char>& blob)
{
	appendUInt32(data, (uint32_t)blob.size());
	if (!blob.empty())
		appendBytes(data, blob.data(), blob.size());
}

static bool readBlob(const vector<char>& data, size_t& offset, vector<char>& blob)
{
	uint32_t size = 0;
	return readUInt32(data, offset, size) && readBytes(data, offset, size, blob);
}

static void appendParamMap(vector<char>& data, const unordered_map<wstring, float>& paramMap)
{
	appendUInt32(data, (uint32_t)paramMap.size());
	for (const auto& it : paramMap)
	{
		appendUInt32(data, (uint32_t)it.first.size());
		if (!it.first.empty())
			appendBytes(data, it.first.data(), it.first.size() * sizeof(wchar_t));
		appendBytes(data, &it.second, sizeof(it.second));
	}
}

static bool readParamMap(const vector<char>& data, size_t& offset, unordered_map<wstring, float>& paramMap)
{
	uint32_t count = 0;
	if (!readUInt32(data, offset, count) || count > 65536)
		return false;

	paramMap.clear();
	for (uint32_t i = 0; i < count; ++i)
	{
		uint32_t length = 0;
		if (!readUInt32(data, offset, length) || length > 1024 * 1024)
			return false;
		if (offset + length * sizeof(wchar_t) + sizeof(float) > data.size())
			return false;

		wstring key(length, L'\0');
		if (length > 0)
			memcpy(&key[0], data.data() + offset, length * sizeof(wchar_t));
		offset += length * sizeof(wchar_t);

		float value = 0.0f;
		memcpy(&value, data.data() + offset, sizeof(value));
		offset += sizeof(value);
		paramMap[key] = value;
	}
	return true;
}

static bool hasEnvelopeMagic(const vector<char>& data, const char* magic)
{
	const size_t magicLength = strlen(magic);
	return data.size() >= magicLength && memcmp(data.data(), magic, magicLength) == 0;
}

static vector<char> makeVST2Envelope(const vector<char>& chunk, const unordered_map<wstring, float>& paramMap)
{
	vector<char> data;
	appendBytes(data, vst2StateEnvelopeMagic, strlen(vst2StateEnvelopeMagic));
	appendBlob(data, chunk);
	appendParamMap(data, paramMap);
	return data;
}

static bool parseVST2Envelope(const vector<char>& data, vector<char>& chunk, unordered_map<wstring, float>& paramMap)
{
	if (!hasEnvelopeMagic(data, vst2StateEnvelopeMagic))
		return false;
	size_t offset = strlen(vst2StateEnvelopeMagic);
	return readBlob(data, offset, chunk) && readParamMap(data, offset, paramMap);
}

static vector<char> makeVST3Envelope(const vector<char>& componentState, const vector<char>& controllerState, const unordered_map<wstring, float>& paramMap,const wstring& uid)
{
	vector<char> data;
	appendBytes(data, vst3StateEnvelopeMagic, strlen(vst3StateEnvelopeMagic));
    const string asciiUid(uid.begin(),uid.end());
    appendBytes(data,asciiUid.data(),asciiUid.size());
	appendBlob(data, componentState);
	appendBlob(data, controllerState);
	appendParamMap(data, paramMap);
	return data;
}

static bool parseVST3Envelope(const vector<char>& data, vector<char>& componentState, vector<char>& controllerState, unordered_map<wstring, float>& paramMap,wstring& uid)
{
    const bool legacy=hasEnvelopeMagic(data,vst3LegacyStateEnvelopeMagic);
    if(!legacy && !hasEnvelopeMagic(data,vst3StateEnvelopeMagic))return false;
    size_t offset=strlen(legacy ? vst3LegacyStateEnvelopeMagic : vst3StateEnvelopeMagic);
    uid.clear();
    if(!legacy) {
        if(data.size()<offset+32)return false;
        uid.assign(data.begin()+offset,data.begin()+offset+32);offset+=32;
    }
	return readBlob(data, offset, componentState) && readBlob(data, offset, controllerState) && readParamMap(data, offset, paramMap);
}

class VSTPluginInstance::VST3HostContext : public IHostApplication, public IComponentHandler, public IComponentHandler2, public IPlugFrame, public IPlugInterfaceSupport
{
public:
	VST3HostContext(VSTPluginInstance* instance) : instance(instance) {}

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		QUERY_INTERFACE(iid, obj, FUnknown::iid, IHostApplication)
		QUERY_INTERFACE(iid, obj, IHostApplication::iid, IHostApplication)
        if(instance){
		    QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
		    QUERY_INTERFACE(iid, obj, IComponentHandler2::iid, IComponentHandler2)
		    QUERY_INTERFACE(iid, obj, IPlugFrame::iid, IPlugFrame)
        }
		QUERY_INTERFACE(iid, obj, IPlugInterfaceSupport::iid, IPlugInterfaceSupport)
		*obj = NULL;
		return kNoInterface;
	}

	uint32 PLUGIN_API addRef() override { return InterlockedIncrement(&refCount); }
	uint32 PLUGIN_API release() override
	{
		uint32 result = InterlockedDecrement(&refCount);
		if (result == 0)
			delete this;
		return result;
	}

	tresult PLUGIN_API getName(String128 name) override
	{
		wcsncpy_s((wchar_t*)name, 128, L"Equalizer APO", _TRUNCATE);
		return kResultOk;
	}

	tresult PLUGIN_API createInstance(TUID cid, TUID iid, void** obj) override
    {
        if(!obj) return kInvalidArgument;
        *obj=nullptr;
        if(insidePluginProcess) return kNotImplemented;
        if(FUnknownPrivate::iidEqual(cid,IMessage::iid) && FUnknownPrivate::iidEqual(iid,IMessage::iid))
            *obj=static_cast<IMessage*>(new EqApoHost::Message);
        else if(FUnknownPrivate::iidEqual(cid,IAttributeList::iid) && FUnknownPrivate::iidEqual(iid,IAttributeList::iid))
            *obj=static_cast<IAttributeList*>(new EqApoHost::Attributes);
        return *obj ? kResultOk : kNoInterface;
    }

	tresult PLUGIN_API beginEdit(ParamID) override {if(instance)instance->activeEditGestures.fetch_add(1,std::memory_order_release);return kResultOk;}
    tresult PLUGIN_API isPlugInterfaceSupported(const TUID iid) override {
        return FUnknownPrivate::iidEqual(iid,IComponent::iid) ||
            FUnknownPrivate::iidEqual(iid,IAudioProcessor::iid) ||
            FUnknownPrivate::iidEqual(iid,IEditController::iid) ||
            FUnknownPrivate::iidEqual(iid,Steinberg::Vst::IConnectionPoint::iid) ||
            FUnknownPrivate::iidEqual(iid,IPlugView::iid) ? kResultTrue : kResultFalse;
    }
	tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
	{
		if (instance != NULL)
		{
			if(!instance->routeEditsThroughSink)instance->queueVST3ParameterChange(id, value);
			instance->notifyParameterEdit(id,value);
			instance->onAutomate();
		}
		return kResultOk;
	}
    void finishGesture(){if(instance){if(instance->activeEditGestures.load()>0)instance->activeEditGestures.fetch_sub(1,std::memory_order_release);instance->onAutomate();}}
	tresult PLUGIN_API endEdit(ParamID) override {finishGesture();return kResultOk;}
    tresult PLUGIN_API setDirty(TBool state) override {
        if(state && instance)instance->onAutomate();
        return kResultOk;
    }
    tresult PLUGIN_API requestOpenEditor(FIDString) override { return kNotImplemented; }
    tresult PLUGIN_API startGroupEdit() override {if(instance)instance->activeEditGestures.fetch_add(1,std::memory_order_release);return kResultOk;}
    tresult PLUGIN_API finishGroupEdit() override {finishGesture();return kResultOk;}
	tresult PLUGIN_API restartComponent(int32 flags) override {
        if(instance) instance->restartFlags.fetch_or(static_cast<uint32>(flags),std::memory_order_release);
        return kResultOk;
    }

	tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override
	{
        if(insidePluginProcess)return kNotImplemented;
        if(!view || !newSize || !instance) return kInvalidArgument;
        if(instance->resizingEditor) return kResultOk;
        instance->resizingEditor=true;
        ViewRect constrained=*newSize;
        // Plugin-initiated sizes are already negotiated (notably after a
        // content-scale change). Constraints apply to host/user resizing;
        // applying them here can clamp a new physical size to the old size.
        if(constrained.getWidth()<=0 || constrained.getHeight()<=0 || constrained.getWidth()>32767 || constrained.getHeight()>32767) {
            instance->resizingEditor=false;
            return kInvalidArgument;
        }
        instance->onSizeWindow(constrained.getWidth(),constrained.getHeight());
        const auto result=view->onSize(&constrained);
        instance->resizingEditor=false;
        return result;
    }

private:
	volatile LONG refCount = 1;
	VSTPluginInstance* instance;
};

static intptr_t callback(struct vst_effect_t* effect, int32_t opcode, int32_t index, int64_t value, const char* ptr, float opt)
{
	VSTPluginInstance* instance = effect != NULL ? (VSTPluginInstance*)effect->host_internal : NULL;

	switch (opcode)
	{
	case VST_HOST_OPCODE_VST_VERSION:
		return VST_VERSION_2_4_0_0;

	case VST_HOST_OPCODE_CURRENT_EFFECT_ID:
		return equalizerApoVSTID;

	case VST_EFFECT_OPCODE_PRODUCT_NAME:
		strcpy_s((char*) ptr, 64, "Equalizer APO");
		return 1;

	case VST_EFFECT_OPCODE_VENDOR_VERSION:
		return (intptr_t) (MAJOR << 24 | MINOR << 16 | REVISION << 8 | 0);

	case VST_HOST_OPCODE_PIN_CONNECTED:
		if (instance != NULL)
			return index < instance->getUsedChannelCount() ? 0 : 1;
		else
			return 0;

	case VST_HOST_OPCODE_IO_NEED_IDLE:
		if(instance)instance->requestLegacyIdle();
		return instance ? 1 : 0;
	case VST_HOST_OPCODE_IO_MODIFIED:
        // Dynamic VST2 bus changes are not advertised by this host.
        // Returning success without reallocating the negotiated layout is unsafe.
        return 0;

	case VST_HOST_OPCODE_EDITOR_UPDATE:
		return 1; // Never call the editor recursively from a plugin callback.

	case VST_HOST_OPCODE_GET_TIME:
		if (instance != NULL) {
			return reinterpret_cast<intptr_t>(instance->getTimeInfo());
		}
		return 0;

	case VST_HOST_OPCODE_GET_SAMPLE_RATE:
		if (instance != NULL)
			return (intptr_t)instance->getSampleRate();
		return 0;

	case VST_HOST_OPCODE_GET_ACTIVE_THREAD:
		if (instance != NULL)
			return instance->getProcessLevel();
		return 0;

	case VST_HOST_OPCODE_LANGUAGE:
		if (instance != NULL)
			return instance->getLanguage();
		return 0;

	case VST_HOST_OPCODE_GET_REPLACE_OR_ACCUMULATE:
		return 1;

	case VST_HOST_OPCODE_EDITOR_RESIZE:
		if (instance != NULL)
		{
			instance->onSizeWindow((int)index, (int)value);
			return 0;
		}
		return 1;

	case VST_HOST_OPCODE_SUPPORTS:
		{
			const char* s = ptr;
            if (!s) return 0;
			if (strcmp(s, "startStopProcess") == 0 ||
				strcmp(s, "sizeWindow") == 0)
				return 1;
		}
		return 0;

	case VST_HOST_OPCODE_AUTOMATE:
		if (instance != NULL)
        {instance->notifyParameterEdit(std::uint32_t(index),opt);instance->onAutomate();}
		return 0;

	case VST_HOST_OPCODE_PARAM_START_EDIT:
	case VST_HOST_OPCODE_PARAM_STOP_EDIT:
	case VST_HOST_OPCODE_KEEPALIVE_OR_IDLE:
	case VST_HOST_OPCODE_WANT_MIDI:
		return 0;
	}

	return 0;
}

static BOOL CALLBACK showChildWindow(HWND hWnd, LPARAM)
{
	ShowWindow(hWnd, SW_SHOW);
	return TRUE;
}

static const wchar_t* vst3EditorHostWindowClass = L"EqualizerAPOVST3EditorHost";

static void registerVST3EditorHostWindowClass()
{
	static bool registered = false;
	if (registered)
		return;

	WNDCLASSW wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = DefWindowProcW;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = vst3EditorHostWindowClass;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	RegisterClassW(&wc);
	registered = true;
}

static std::atomic<std::uint64_t> livePluginInstances{0};
std::uint64_t VSTPluginInstance::getLiveInstanceCount() noexcept {return livePluginInstances.load(std::memory_order_relaxed);}

VSTPluginInstance::VSTPluginInstance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel, int vst3ClassIndex)
	: library(library), processLevel(processLevel), vst3ClassIndex(vst3ClassIndex)
{
    livePluginInstances.fetch_add(1,std::memory_order_relaxed);
}

VSTPluginInstance::~VSTPluginInstance()
{
    parameterSink=nullptr;
	automateFunc = nullptr;
	sizeWindowFunc = nullptr;
	releaseVST3();
	if (effect != NULL)
	{
		effect->control(effect, VST_EFFECT_OPCODE_DESTROY, 0, 0, NULL, 0.0f);
		effect = NULL;
	}
    livePluginInstances.fetch_sub(1,std::memory_order_relaxed);
}

bool VSTPluginInstance::initialize()
{
	if (library->isVST3())
		return initializeVST3();
	return initializeVST2();
}

bool VSTPluginInstance::initializeVST2()
{
	bool result = true;

	__try
	{
		effect = library->VSTPluginMain(callback);
		effect->host_internal = this;
		if (effect->magic_number == VST_MAGICNUMBER)
		{
			effect->control(effect, VST_EFFECT_OPCODE_INITIALIZE, 0, 0, NULL, 0.0f);

			usedChannelCount = max(numInputs(), numOutputs());
		}
		else
		{
			effect = NULL;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		result = false;
	}

	return result;
}

Steinberg::FUnknown* VSTPluginInstance::createFactoryHostContext(){
    return static_cast<IHostApplication*>(new VST3HostContext(nullptr));
}
bool VSTPluginInstance::initializeVST3()
{
	if (!library->getFactory() || vst3ClassIndex < 0 || vst3ClassIndex >= library->getVST3ClassCount())
		return false;
	vst3HostContext = new VST3HostContext(this);

	const PClassInfo& classInfo = library->getVST3ClassInfo(vst3ClassIndex);
	FUID componentId(classInfo.cid);
	TUID componentIid;
	IComponent::iid.toTUID(componentIid);
	if (library->getFactory()->createInstance(componentId, componentIid, (void**)&vst3Component) != kResultOk || vst3Component == NULL)
		return false;

	vst3Component->setIoMode(kSimple);
	if (vst3Component->initialize(static_cast<IHostApplication*>(vst3HostContext)) != kResultOk)
		return false;

	TUID processorIid;
	IAudioProcessor::iid.toTUID(processorIid);
	if (vst3Component->queryInterface(processorIid, (void**)&vst3Processor) != kResultOk || vst3Processor == NULL)
		return false;

    // SDK PlugProvider first checks a combined processor/controller. Creating
    // another controller first separates its non-parameter buttons from DSP.
    TUID controllerIid;IEditController::iid.toTUID(controllerIid);
    if(vst3Component->queryInterface(controllerIid,(void**)&vst3Controller)==kResultOk && vst3Controller){
        controllerIsComponent=true;
    }else{
        vst3Controller=nullptr;TUID controllerClassId{};
        if(vst3Component->getControllerClassId(controllerClassId)==kResultOk)
            library->getFactory()->createInstance(controllerClassId,controllerIid,(void**)&vst3Controller);
    }
	if (vst3Controller != NULL)
	{
		if (!controllerIsComponent && vst3Controller->initialize(static_cast<IHostApplication*>(vst3HostContext)) != kResultOk) return false;
        std::vector<ParamID> registeredIds;
        const auto parameterCount=vst3Controller->getParameterCount();
        if(parameterCount<0 || parameterCount>65536) return false;
        registeredIds.reserve(parameterCount);
        for(int32 i=0;i<parameterCount;++i) {
            ParameterInfo info{};
            if(vst3Controller->getParameterInfo(i,info)==kResultOk) registeredIds.push_back(info.id);
        }
        latestOutputParameters.prepare(registeredIds);
        latestControllerInputs.prepare(registeredIds);
        latestInputParameters.prepare(std::move(registeredIds));
		vst3Controller->setComponentHandler(vst3HostContext);

		TUID connectionPointIid;
		Steinberg::Vst::IConnectionPoint::iid.toTUID(connectionPointIid);
		if (!controllerIsComponent && vst3Component->queryInterface(connectionPointIid, (void**)&vst3ComponentConnection) == kResultOk
			&& vst3Controller->queryInterface(connectionPointIid, (void**)&vst3ControllerConnection) == kResultOk)
		{
			vst3ComponentConnection->connect(vst3ControllerConnection);
			vst3ControllerConnection->connect(vst3ComponentConnection);
		}

		VST3MemoryStream* stream = new VST3MemoryStream();
		if (vst3Component->getState(stream) == kResultOk)
		{
			stream->seek(0, IBStream::kIBSeekSet);
			vst3Controller->setComponentState(stream);
		}
		stream->release();

	}

	vst3InputBusCount = max(0, vst3Component->getBusCount(kAudio, kInput));
	vst3OutputBusCount = max(0, vst3Component->getBusCount(kAudio, kOutput));
	configureVST3Buses(2);

	vst3SupportsDouble = vst3Processor->canProcessSampleSize(kSample64) == kResultOk;
	if (!vst3SupportsDouble && vst3Processor->canProcessSampleSize(kSample32) != kResultOk)
		return false;

	usedChannelCount = max(numInputs(), numOutputs());

	return true;
}

int VSTPluginInstance::numInputs() const
{
	if (library->isVST3())
		return vst3InputChannelCount;
	if (effect == NULL)
		return 0;

	return effect->num_inputs;
}

int VSTPluginInstance::numOutputs() const
{
	if (library->isVST3())
		return vst3OutputChannelCount;
	if (effect == NULL)
		return 0;

	return effect->num_outputs;
}

bool VSTPluginInstance::canReplacing() const
{
	if (library->isVST3())
		return true;
	if (effect == NULL)
		return true;

	return (effect->flags & VST_EFFECT_FLAG_SUPPORTS_FLOAT) != 0;
}

int VSTPluginInstance::uniqueID() const
{
	if (library->isVST3())
	{
		const PClassInfo& classInfo = library->getVST3ClassInfo(vst3ClassIndex);
		int result = 0;
		memcpy(&result, classInfo.cid, sizeof(result));
		return result;
	}
	if (effect == NULL)
		return 0;

	return effect->unique_id;
}

std::wstring VSTPluginInstance::getName() const
{
	if (library->isVST3())
		return StringHelper::toWString(library->getVST3ClassInfo(vst3ClassIndex).name, CP_UTF8);
	if (effect == NULL)
		return L"";

	char buf[256];
	memset(buf, 0, sizeof(buf));
	effect->control(effect, VST_EFFECT_OPCODE_EFFECT_NAME, 0, 0, buf, 0.0f);
	buf[255] = '\0'; // just to be sure

	return StringHelper::toWString(buf, CP_UTF8);
}

int VSTPluginInstance::getUsedChannelCount() const
{
	return usedChannelCount;
}

void VSTPluginInstance::setUsedChannelCount(int count)
{
	usedChannelCount = count;
}

float VSTPluginInstance::getSampleRate() const
{
	return sampleRate;
}

int VSTPluginInstance::getProcessLevel() const
{
	return processLevel;
}

bool VSTPluginInstance::canDoubleReplacing() const
{
	if (library->isVST3())
		return vst3SupportsDouble && preferDoubleProcessing;
	return preferDoubleProcessing && (effect->flags & VST_EFFECT_FLAG_SUPPORTS_DOUBLE) != 0;
}

int VSTPluginInstance::getInitialDelay() const
{
	if (library->isVST3())
		return vst3Processor != NULL ? (int)vst3Processor->getLatencySamples() : 0;
	if (effect == NULL)
		return 0;

	return effect->delay;
}

void VSTPluginInstance::setProcessLevel(int value)
{
	processLevel = value;
}

int VSTPluginInstance::getLanguage() const
{
	return language;
}

void VSTPluginInstance::setLanguage(int value)
{
	language = value;
}

void VSTPluginInstance::prepareForProcessing(float sampleRate, int blockSize, bool preferDouble)
{
    preferDoubleProcessing=preferDouble;
	if (library->isVST3())
	{
		if (vst3Processor == NULL || vst3Component == NULL)
			return;

		this->sampleRate = sampleRate;
        preparedBlockSize=blockSize;
		configureVST3Buses(usedChannelCount > 0 ? usedChannelCount : max(vst3InputChannelCount, vst3OutputChannelCount));
		ProcessSetup setup;
		setup.processMode = processLevel == 4 ? kOffline : kRealtime;
		setup.symbolicSampleSize = vst3SupportsDouble && preferDoubleProcessing ? kSample64 : kSample32;
		setup.maxSamplesPerBlock = blockSize;
		setup.sampleRate = sampleRate;
		processingSetupValid = processingSetupValid && vst3Processor->setupProcessing(setup)==kResultOk;
		vst3SamplePosition = 0;
		return;
	}

	if (effect == NULL)
		return;

	this->sampleRate = sampleRate;
    vstTime.sampleRate=sampleRate; vstTime.samplePos=0; vstTime.flags=0;
	effect->control(effect, VST_EFFECT_OPCODE_SET_SAMPLE_RATE, 0, 0, NULL, sampleRate);
	effect->control(effect, VST_EFFECT_OPCODE_SET_BLOCK_SIZE, 0, blockSize, NULL, 0.0f);
}

void VSTPluginInstance::writeToEffect(const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap)
{
    std::lock_guard<std::recursive_mutex> mirrorLock(mirrorMutex);
	if (library->isVST3())
	{
        stateRestoreValid=chunkData.empty();
		if (chunkData != L"")
		{
			vector<char> data;
			if (base64Decode(chunkData, data))
			{
				vector<char> componentState;
				vector<char> controllerState;
				unordered_map<wstring, float> envelopeParamMap;
				wstring stateUid;
                const bool isEnvelope = parseVST3Envelope(data, componentState, controllerState, envelopeParamMap,stateUid);
                if((hasEnvelopeMagic(data,vst3StateEnvelopeMagic)||hasEnvelopeMagic(data,vst3LegacyStateEnvelopeMagic)) && !isEnvelope)return;
                if(!stateUid.empty() && _wcsicmp(stateUid.c_str(),library->getVST3ClassUID(vst3ClassIndex).c_str())!=0)return;
				if (!isEnvelope)
					componentState = data;

				if (!componentState.empty())
				{
					VST3MemoryStream* stream = new VST3MemoryStream(componentState);
                    const bool accepted=vst3Component && vst3Component->setState(stream)==kResultOk;
					stream->seek(0, IBStream::kIBSeekSet);
					if (vst3Controller != NULL)
						vst3Controller->setComponentState(stream);
					stream->release();
                    if(!accepted)return;
				}

				if (!controllerState.empty() && vst3Controller != NULL)
				{
					VST3MemoryStream* stream = new VST3MemoryStream(controllerState);
                    const auto accepted=vst3Controller->setState(stream)==kResultOk;
					stream->release();
                    if(!accepted)return;
				}

				unordered_map<wstring,float> paramsToApply=isEnvelope ? envelopeParamMap : unordered_map<wstring,float>();
                stateRestoreValid=true;
                for(const auto& explicitParameter:paramMap)paramsToApply[explicitParameter.first]=explicitParameter.second;
				if (vst3Controller != NULL)
				{
					for (auto it : paramsToApply)
					{
						if (!it.first.empty() && it.first[0] == vst3ParamIdPrefix[0])
						{
							ParamID id=0;
							if (parseParameterId(it.first,id) && std::isfinite(it.second) && it.second >= 0 && it.second <= 1)
							{
								vst3Controller->setParamNormalized(id, it.second);
								queueVST3ParameterChange(id, it.second);
							}
							continue;
						}
					}
				}
			}
		}
		else if (vst3Controller != NULL)
		{
			for (auto it : paramMap)
			{
				if (!it.first.empty() && it.first[0] == vst3ParamIdPrefix[0])
				{
					ParamID id=0;
					if (parseParameterId(it.first,id) && std::isfinite(it.second) && it.second >= 0 && it.second <= 1)
					{
						vst3Controller->setParamNormalized(id, it.second);
						queueVST3ParameterChange(id, it.second);
					}
					continue;
				}
			}
		}
		// Establish a baseline after restoration. A general controller refresh
		// must transmit changed controls, not thousands of unchanged defaults.
        notifiedParameterValues.clear();
        if(vst3Controller)for(int32 i=0;i<vst3Controller->getParameterCount();++i) {
            ParameterInfo info{};
            if(vst3Controller->getParameterInfo(i,info)==kResultOk && !(info.flags&ParameterInfo::kIsReadOnly))
                notifiedParameterValues[info.id]=vst3Controller->getParamNormalized(info.id);
        }
		return;
	}

	if (effect == NULL)
		return;

	if (effect->flags & VST_EFFECT_FLAG_CHUNKS)
	{
		if (chunkData != L"")
		{
			vector<char> data;
			if (base64Decode(chunkData, data))
			{
				vector<char> chunk;
				unordered_map<wstring, float> envelopeParamMap;
				const bool isEnvelope = parseVST2Envelope(data, chunk, envelopeParamMap);
				if (!isEnvelope)
					chunk = data;

				if (!chunk.empty())
				{
					effect->control(effect, VST_EFFECT_OPCODE_SET_CHUNK_DATA, 1, (intptr_t)chunk.size(), chunk.data(), 0.0f);
				}

				if (isEnvelope)
				{
					for (int i = 0; i < effect->num_params; i++)
					{
						char buf[256];
						effect->control(effect, VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
						buf[255] = '\0';
						auto it = envelopeParamMap.find(L"#" + to_wstring(i));
                        if(it==envelopeParamMap.end())it=envelopeParamMap.find(StringHelper::toWString(buf, CP_UTF8));
						if (it != envelopeParamMap.end() && std::isfinite(it->second) && it->second>=0 && it->second<=1)
							effect->set_parameter(effect, i, it->second);
					}
				}
			}
		}
	}
	// Explicit edits override the saved preset, just as for VST3. The chunk
	// restores opaque state first; skipping these edits silently loses changes.
	{
		for (int i = 0; i < effect->num_params; i++)
		{
			char buf[256];
			effect->control(effect, VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
			buf[255] = '\0'; // just to be sure
			wstring name = StringHelper::toWString(buf, CP_UTF8);
			auto it = paramMap.find(L"#" + to_wstring(i));
            if(it==paramMap.end())it=paramMap.find(name); // Read legacy configurations.
			if (it != paramMap.end() && std::isfinite(it->second) && it->second>=0 && it->second<=1)
				effect->set_parameter(effect, i, it->second);
		}
	}
}

std::vector<VSTPluginInstance::ParameterMetadata> VSTPluginInstance::getParameterMetadata() const
{
    std::vector<ParameterMetadata> result;
    if (library->isVST3() && vst3Controller) {
        for (int32 i=0;i<vst3Controller->getParameterCount();++i) {
            ParameterInfo info{};
            if (vst3Controller->getParameterInfo(i,info)==kResultOk) {
                info.title[127]=0;
                result.push_back({info.id,reinterpret_cast<const wchar_t*>(info.title),vst3Controller->getParamNormalized(info.id),info.flags,info.stepCount});
            }
        }
    } else if (effect) {
        for (int i=0;i<effect->num_params;++i) {
            char title[256]{};
            effect->control(effect,VST_EFFECT_OPCODE_PARAM_GET_NAME,i,0,title,0);title[255]=0;
            result.push_back({std::uint32_t(i),StringHelper::toWString(title,CP_UTF8),effect->get_parameter(effect,i),0});
        }
    }
    return result;
}

std::wstring VSTPluginInstance::getParameterValueText(std::uint32_t id,double value) const
{
    if(insidePluginProcess || !std::isfinite(value) || value<0 || value>1 || !vst3Controller)return {};
    String128 text{};
    if(vst3Controller->getParamStringByValue(id,value,text)!=kResultOk)return {};
    text[127]=0;
    return reinterpret_cast<const wchar_t*>(text);
}

std::wstring VSTPluginInstance::getCurrentParameterText(std::uint32_t id) const
{
    if(insidePluginProcess)return {};
    if(vst3Controller)return getParameterValueText(id,vst3Controller->getParamNormalized(id));
    if(!effect || id>=std::uint32_t(effect->num_params))return {};
    char text[256]{};
    effect->control(effect,VST_EFFECT_OPCODE_PARAM_VALUE_TO_STRING,int(id),0,text,0);
    text[255]=0;
    return StringHelper::toWString(text,CP_UTF8);
}

void VSTPluginInstance::readFromEffect(std::wstring& chunkData, std::unordered_map<std::wstring, float>& paramMap) const
{
    std::lock_guard<std::recursive_mutex> mirrorLock(mirrorMutex);
	if (library->isVST3())
	{
		chunkData = L"";
		paramMap.clear();

		vector<char> componentState;
		vector<char> controllerState;
		if (vst3Component != NULL)
		{
			VST3MemoryStream* stream = new VST3MemoryStream();
			if (vst3Component->getState(stream) == kResultOk && !stream->getData().empty())
				componentState = stream->getData();
			stream->release();
		}

		if (vst3Controller != NULL)
		{
			VST3MemoryStream* stream = new VST3MemoryStream();
			if (vst3Controller->getState(stream) == kResultOk && !stream->getData().empty())
				controllerState = stream->getData();
			stream->release();

			for (int32 i = 0; i < vst3Controller->getParameterCount(); i++)
			{
				ParameterInfo info;
				if (vst3Controller->getParameterInfo(i, info) == kResultOk && !(info.flags & ParameterInfo::kIsReadOnly))
				{
					paramMap[wstring(vst3ParamIdPrefix) + to_wstring(info.id)] = (float)vst3Controller->getParamNormalized(info.id);
				}
			}
		}

		if (!componentState.empty() || !controllerState.empty())
		{
			vector<char> envelope = makeVST3Envelope(componentState, controllerState, paramMap,library->getVST3ClassUID(vst3ClassIndex));
			base64Encode(envelope, chunkData);
		}
		return;
	}

	if (effect == NULL)
		return;

	chunkData = L"";
	paramMap.clear();

	if (effect->flags & VST_EFFECT_FLAG_CHUNKS)
	{
		BYTE* chunk = NULL;
		int size = (int)effect->control(effect, VST_EFFECT_OPCODE_GET_CHUNK_DATA, 1, 0, &chunk, 0.0f);
		vector<char> chunkDataRaw;
		if (chunk != NULL && size > 0)
			chunkDataRaw.assign((char*)chunk, (char*)chunk + size);

		for (int i = 0; i < effect->num_params; i++)
		{
			char buf[256];
			effect->control(effect, VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
			buf[255] = '\0';
			float value = effect->get_parameter(effect, i);
			paramMap[L"#" + to_wstring(i)] = value;
		}

		if (!chunkDataRaw.empty())
		{
			vector<char> envelope = makeVST2Envelope(chunkDataRaw, paramMap);
			base64Encode(envelope, chunkData);
		}
	}
	else
	{
		for (int i = 0; i < effect->num_params; i++)
		{
			char buf[256];
			effect->control(effect, VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
			buf[255] = '\0'; // just to be sure
			float value = effect->get_parameter(effect, i);
			paramMap[L"#" + to_wstring(i)] = value;
		}
	}
}

void VSTPluginInstance::startProcessing()
{
	if (library->isVST3())
	{
		if (processingSetupValid && vst3Component != NULL && !vst3Active)
		{
			vst3Active = vst3Component->setActive(true)==kResultOk;
		}
		if (vst3Active && vst3Processor != NULL && !vst3Processing)
		{
			vst3Processing = vst3Processor->setProcessing(true)==kResultOk;
		}
		return;
	}

	if (effect == NULL)
		return;

	effect->control(effect, VST_EFFECT_OPCODE_SUSPEND, 0, 1, NULL, 0.0f);
    vstTime.flags|=(1<<1); // VST2 transportPlaying: this instance now renders the running stream.
	effect->control(effect, VST_EFFECT_OPCODE_PROCESS_BEGIN, 0, 0, NULL, 0.0f);
}

void VSTPluginInstance::processReplacing(float** inputArray, float** outputArray, int frameCount)
{
    PluginProcessScope processScope;
    if (library->isVST3()) {
        if(!vst3Processor || !isProcessingValid()) return;
        for(std::size_t i=0;i<vst3InputBuffers.size();++i) {
            auto& bus=vst3InputBuffers[i]; bus.silenceFlags=0;
            bus.numChannels=(i==vst3InputMainIndex ? numInputs() : 0);
            bus.channelBuffers32=(i==vst3InputMainIndex ? inputArray : nullptr);
        }
        for(std::size_t i=0;i<vst3OutputBuffers.size();++i) {
            auto& bus=vst3OutputBuffers[i]; bus.silenceFlags=0;
            bus.numChannels=(i==vst3OutputMainIndex ? numOutputs() : 0);
            bus.channelBuffers32=(i==vst3OutputMainIndex ? outputArray : nullptr);
        }
        vst3InputChanges.clear(); vst3OutputChanges.clear();
        EqApoRT::ParameterEvent change{};
        for(unsigned n=0;n<4096 && pendingVST3ParameterChanges.pop(change);++n) {
            int32 qi=-1,pi=-1;
            auto* q=vst3InputChanges.addParameterData(change.id,qi);
            if(!q || q->addPoint(frameCount>0 ? (std::min)(change.offset,frameCount-1) : 0,change.value,pi)!=kResultOk)
            {
                parameterOverflows.fetch_add(1,std::memory_order_relaxed);
                latestInputParameters.markOverflow(change.id);
            }
        }
        latestInputParameters.drain(256,[&](ParamID id,double value) noexcept {
            int32 qi=-1,pi=-1;auto* q=vst3InputChanges.addParameterData(id,qi);
            return q && q->addPoint(0,value,pi)==kResultOk;
        });
        ProcessData data{};
        data.processMode=processLevel==4 ? kOffline : kRealtime;
        data.symbolicSampleSize=kSample32; data.numSamples=frameCount;
        data.numInputs=static_cast<int32>(vst3InputBuffers.size());
        data.numOutputs=static_cast<int32>(vst3OutputBuffers.size());
        data.inputs=vst3InputBuffers.data(); data.outputs=vst3OutputBuffers.data();
        data.inputParameterChanges=&vst3InputChanges; data.outputParameterChanges=&vst3OutputChanges;
        data.inputEvents=&emptyVST3EventList;
        data.processContext=&vst3ProcessContext;
        vst3ProcessContext.state=ProcessContext::kContTimeValid|ProcessContext::kPlaying;
        vst3ProcessContext.sampleRate=sampleRate;
        vst3ProcessContext.projectTimeSamples=vst3SamplePosition;
        vst3ProcessContext.continousTimeSamples=vst3SamplePosition;
        if(vst3Processor->process(data)!=kResultOk) {
            processingSetupValid=false;
            for(int c=0;c<numOutputs();++c) std::memset(outputArray[c],0,frameCount*sizeof(*outputArray[c]));
        }
        vst3SamplePosition+=frameCount;
        for(int32 qi=0;qi<vst3OutputChanges.getParameterCount();++qi) {
            auto* q=vst3OutputChanges.getParameterData(qi);
            for(int32 pi=0;pi<q->getPointCount();++pi) {
                int32 offset=0; double value=0;
                if(q->getPoint(pi,offset,value)==kResultOk && std::isfinite(value)) {
                    latestOutputParameters.remember(q->getParameterId(),value,false);
                    if(!outputVST3ParameterChanges.push({q->getParameterId(),value,offset})) {
                        latestOutputParameters.markOverflow(q->getParameterId());
                        outputMailboxPending.store(true,std::memory_order_release);
                        parameterOverflows.fetch_add(1,std::memory_order_relaxed);
                    }
                }
            }
        }
        return;
    }

	if (effect == NULL)
		return;

	effect->process_float(effect, inputArray, outputArray, frameCount);
    vstTime.samplePos += frameCount;
}

void VSTPluginInstance::processDoubleReplacing(double** inputArray, double** outputArray, int frameCount)
{
    PluginProcessScope processScope;
    if (library->isVST3()) {
        if(!vst3Processor || !isProcessingValid()) return;
        for(std::size_t i=0;i<vst3InputBuffers.size();++i) {
            auto& bus=vst3InputBuffers[i]; bus.silenceFlags=0;
            bus.numChannels=(i==vst3InputMainIndex ? numInputs() : 0);
            bus.channelBuffers64=(i==vst3InputMainIndex ? inputArray : nullptr);
        }
        for(std::size_t i=0;i<vst3OutputBuffers.size();++i) {
            auto& bus=vst3OutputBuffers[i]; bus.silenceFlags=0;
            bus.numChannels=(i==vst3OutputMainIndex ? numOutputs() : 0);
            bus.channelBuffers64=(i==vst3OutputMainIndex ? outputArray : nullptr);
        }
        vst3InputChanges.clear(); vst3OutputChanges.clear();
        EqApoRT::ParameterEvent change{};
        for(unsigned n=0;n<4096 && pendingVST3ParameterChanges.pop(change);++n) {
            int32 qi=-1,pi=-1;
            auto* q=vst3InputChanges.addParameterData(change.id,qi);
            if(!q || q->addPoint(frameCount>0 ? (std::min)(change.offset,frameCount-1) : 0,change.value,pi)!=kResultOk)
            {
                parameterOverflows.fetch_add(1,std::memory_order_relaxed);
                latestInputParameters.markOverflow(change.id);
            }
        }
        latestInputParameters.drain(256,[&](ParamID id,double value) noexcept {
            int32 qi=-1,pi=-1;auto* q=vst3InputChanges.addParameterData(id,qi);
            return q && q->addPoint(0,value,pi)==kResultOk;
        });
        ProcessData data{};
        data.processMode=processLevel==4 ? kOffline : kRealtime;
        data.symbolicSampleSize=kSample64; data.numSamples=frameCount;
        data.numInputs=static_cast<int32>(vst3InputBuffers.size());
        data.numOutputs=static_cast<int32>(vst3OutputBuffers.size());
        data.inputs=vst3InputBuffers.data(); data.outputs=vst3OutputBuffers.data();
        data.inputParameterChanges=&vst3InputChanges; data.outputParameterChanges=&vst3OutputChanges;
        data.inputEvents=&emptyVST3EventList;
        data.processContext=&vst3ProcessContext;
        vst3ProcessContext.state=ProcessContext::kContTimeValid|ProcessContext::kPlaying;
        vst3ProcessContext.sampleRate=sampleRate;
        vst3ProcessContext.projectTimeSamples=vst3SamplePosition;
        vst3ProcessContext.continousTimeSamples=vst3SamplePosition;
        if(vst3Processor->process(data)!=kResultOk) {
            processingSetupValid=false;
            for(int c=0;c<numOutputs();++c) std::memset(outputArray[c],0,frameCount*sizeof(*outputArray[c]));
        }
        vst3SamplePosition+=frameCount;
        for(int32 qi=0;qi<vst3OutputChanges.getParameterCount();++qi) {
            auto* q=vst3OutputChanges.getParameterData(qi);
            for(int32 pi=0;pi<q->getPointCount();++pi) {
                int32 offset=0; double value=0;
                if(q->getPoint(pi,offset,value)==kResultOk && std::isfinite(value)) {
                    latestOutputParameters.remember(q->getParameterId(),value,false);
                    if(!outputVST3ParameterChanges.push({q->getParameterId(),value,offset})) {
                        latestOutputParameters.markOverflow(q->getParameterId());
                        outputMailboxPending.store(true,std::memory_order_release);
                        parameterOverflows.fetch_add(1,std::memory_order_relaxed);
                    }
                }
            }
        }
        return;
    }

	if (effect == NULL)
		return;

	effect->process_double(effect, inputArray, outputArray, frameCount);
    vstTime.samplePos += frameCount;
}

void VSTPluginInstance::process(float** inputArray, float** outputArray, int frameCount)
{
	if (library->isVST3())
	{
		processReplacing(inputArray, outputArray, frameCount);
		return;
	}

	if (effect == NULL)
		return;

	effect->process(effect, inputArray, outputArray, frameCount);
}

void VSTPluginInstance::stopProcessing()
{
	if (library->isVST3())
	{
		if (vst3Processor != NULL && vst3Processing)
		{
			vst3Processor->setProcessing(false);
			vst3Processing = false;
		}
		if (vst3Component != NULL && vst3Active)
		{
			vst3Component->setActive(false);
			vst3Active = false;
		}
		return;
	}

	if (effect == NULL)
		return;

	effect->control(effect, VST_EFFECT_OPCODE_PROCESS_END, 0, 0, NULL, 0.0f);
    vstTime.flags&=~(1<<1);
	effect->control(effect, VST_EFFECT_OPCODE_SUSPEND, 0, 0, NULL, 0.0f);
}

void VSTPluginInstance::queueVST3ParameterChange(ParamID id, ParamValue value,int sampleOffset)
{
    if(!std::isfinite(value)) return;
    value=(std::max)(0.0,(std::min)(1.0,value));
    latestInputParameters.remember(id,value,false);
    const bool overflow=!pendingVST3ParameterChanges.push({id,value,(std::max)(0,sampleOffset)});
    if(overflow) latestInputParameters.markOverflow(id);
    if(overflow) parameterOverflows.fetch_add(1,std::memory_order_relaxed);
    stateGeneration.fetch_add(1,std::memory_order_release);
}

bool VSTPluginInstance::startEditing(HWND hWnd, short* width, short* height)
{
	if (width != NULL)
		*width = 400;
	if (height != NULL)
		*height = 300;

	if (library->isVST3())
	{
		if (vst3Controller == NULL)
			return false;
		stopEditing();
		vst3View = vst3Controller->createView(ViewType::kEditor);
		if (vst3View == NULL)
			return false;
		vst3View->setFrame(vst3HostContext);
        setEditorScale(float(GetDpiForWindow(hWnd))/96.0f);
		ViewRect rect;
		if (vst3View->getSize(&rect) == kResultOk)
		{
			if (width != NULL)
				*width = (short)max<int32>(1, rect.getWidth());
			if (height != NULL)
				*height = (short)max<int32>(1, rect.getHeight());
		}
		tresult platformResult = vst3View->isPlatformTypeSupported(kPlatformTypeHWND);
		if (platformResult != kResultOk && platformResult != kNotImplemented)
		{
			stopEditing();
			return false;
		}
		registerVST3EditorHostWindowClass();
		vst3EditorHostWindow = CreateWindowExW(0, vst3EditorHostWindowClass, L"",
			WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE,
			0, 0, *width, *height, hWnd, NULL, GetModuleHandleW(NULL), NULL);
		if (vst3EditorHostWindow == NULL)
		{
			stopEditing();
			return false;
		}
		if (vst3View->attached(vst3EditorHostWindow, kPlatformTypeHWND) != kResultOk)
		{
			stopEditing();
			return false;
		}
		if (vst3View->getSize(&rect) == kResultOk)
		{
			if (width != NULL)
				*width = (short)max<int32>(1, rect.getWidth());
			if (height != NULL)
				*height = (short)max<int32>(1, rect.getHeight());
			vst3View->onSize(&rect);
		}
		SetWindowPos(vst3EditorHostWindow, NULL, 0, 0, *width, *height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		EnumChildWindows(vst3EditorHostWindow, showChildWindow, 0);
		watchEditorInput(vst3EditorHostWindow,this);
		return true;
	}

	if (effect == NULL)
		return false;

	vst_rect_t* rect = nullptr;
	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_GET_RECT, 0, 0, &rect, 0.0f);
	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_OPEN, 0, 0, hWnd, 0.0f);
	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_GET_RECT, 0, 0, &rect, 0.0f);

	// Some VST2 editors return no rectangle until after effEditOpen, or do
	// not supply one at all. Never dereference an absent vendor rectangle.
	if (!rect) return true;
	if (width != NULL)
		*width = rect->right - rect->left;
	if (height != NULL)
		*height = rect->bottom - rect->top;
	return true;
}

void VSTPluginInstance::doIdle()
{
    // The plugin has returned from its input callback before this control
    // timer runs. Existing debounce captures complete, unnotified UI state.
    if(editorInteraction.exchange(false,std::memory_order_acq_rel)) {
        stateGeneration.fetch_add(1,std::memory_order_release);
        automationNotification.store(true,std::memory_order_release);
    }
    const auto requestedSize=pendingEditorSize.exchange(0,std::memory_order_acq_rel);
    if(requestedSize)onSizeWindow(int(requestedSize>>32),int(requestedSize&0xffffffff));
    if(automationNotification.exchange(false,std::memory_order_acq_rel) && automateFunc) automateFunc();
	if (library->isVST3())
	{
		// The native view owns invalidation and animation timers. Synchronous
        // recursive painting can reenter its callbacks and block GUI teardown.
		serviceControllerParameterChanges();
		return;
	}

	if (effect == NULL)
		return;

	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_KEEP_ALIVE, 0, 0, NULL, 0.0f);
}

bool VSTPluginInstance::applyParameterChangeOnDsp(std::uint32_t id,double value,int sampleOffset)
{
    if(!std::isfinite(value) || value<0 || value>1)return false;
    if(library->isVST3()) {
        queueVST3ParameterChange(id,value,sampleOffset);
        latestControllerInputs.remember(id,value,true);return true;
    }
    if(!effect || id>=unsigned(effect->num_params))return false;
    effect->set_parameter(effect,id,float(value));return true;
}

void VSTPluginInstance::stopEditing()
{
    unwatchEditorInput(this);
    editorInteraction.store(false,std::memory_order_release);
    editorScale=0;
	if (library->isVST3())
	{
		if (vst3View != NULL)
		{
			vst3View->removed();
			vst3View->setFrame(NULL);
			vst3View->release();
			vst3View = NULL;
		}
		if (vst3EditorHostWindow != NULL)
		{
			DestroyWindow(vst3EditorHostWindow);
			vst3EditorHostWindow = NULL;
		}
		return;
	}

	if (effect == NULL)
		return;

	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_CLOSE, 0, 0, NULL, 0.0f);
}

void VSTPluginInstance::setAutomateFunc(std::function<void()> func)
{
	automateFunc = func;
}

void VSTPluginInstance::setParameterSink(std::function<void(std::uint32_t,double,int)> sink,bool routeThroughSink)
{
    parameterSink=std::move(sink);routeEditsThroughSink=routeThroughSink;
}

void VSTPluginInstance::notifyParameterEdit(std::uint32_t id,double value,int offset)
{
    // Only GUI/controller instances install this sink. Never perform kernel
    // lookup from a plugin callback nested inside DSP processing.
    if(!insidePluginProcess && parameterSink && std::isfinite(value) && value>=0 && value<=1) {
        const auto previous=notifiedParameterValues.find(id);
        if(previous!=notifiedParameterValues.end() && previous->second==value)return;
        notifiedParameterValues[id]=value;parameterSink(id,value,offset);
    }
}

void VSTPluginInstance::onAutomate()
{
    stateGeneration.fetch_add(1,std::memory_order_release);
    automationNotification.store(true,std::memory_order_release);
}

void VSTPluginInstance::setSizeWindowFunc(std::function<void(int, int)> func)
{
	sizeWindowFunc = func;
}

bool VSTPluginInstance::getEditorSize(int* width, int* height)
{
    if(!width || !height)return false;
    int w=0,h=0;
    if(library->isVST3()) {
        ViewRect rect{};
        if(!vst3View || vst3View->getSize(&rect)!=kResultOk)return false;
        w=rect.getWidth();h=rect.getHeight();
    } else {
        vst_rect_t* rect=nullptr;
        if(!effect || !effect->control(effect,VST_EFFECT_OPCODE_EDITOR_GET_RECT,0,0,&rect,0) || !rect)return false;
        w=rect->right-rect->left;h=rect->bottom-rect->top;
    }
    if(w<=0 || h<=0 || w>32767 || h>32767)return false;
    *width=w;*height=h;return true;
}

void VSTPluginInstance::onSizeWindow(int w, int h)
{
    if(insidePluginProcess) {
        if(w>0 && h>0)pendingEditorSize.store((std::uint64_t(unsigned(w))<<32)|unsigned(h),std::memory_order_release);
        return;
    }
	if (vst3EditorHostWindow != NULL)
		SetWindowPos(vst3EditorHostWindow, NULL, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
	if (sizeWindowFunc)
		sizeWindowFunc(w, h);
}

void VSTPluginInstance::releaseVST3()
{
	if (!library->isVST3())
		return;

	automateFunc = nullptr;
	sizeWindowFunc = nullptr;
	stopEditing();
	stopProcessing();

	if (vst3Controller != NULL)
		vst3Controller->setComponentHandler(NULL);

	if (vst3ComponentConnection != NULL && vst3ControllerConnection != NULL)
	{
		vst3ComponentConnection->disconnect(vst3ControllerConnection);
		vst3ControllerConnection->disconnect(vst3ComponentConnection);
	}
	if (vst3ComponentConnection != NULL)
	{
		vst3ComponentConnection->release();
		vst3ComponentConnection = NULL;
	}
	if (vst3ControllerConnection != NULL)
	{
		vst3ControllerConnection->release();
		vst3ControllerConnection = NULL;
	}
	if (vst3Controller != NULL)
	{
		if (!controllerIsComponent) vst3Controller->terminate();
		vst3Controller->release();
		vst3Controller = NULL;
	}
	if (vst3Processor != NULL)
	{
		vst3Processor->release();
		vst3Processor = NULL;
	}
	if (vst3Component != NULL)
	{
		vst3Component->terminate();
		vst3Component->release();
		vst3Component = NULL;
	}
	if (vst3HostContext != NULL)
	{
		vst3HostContext->release();
		vst3HostContext = NULL;
	}
}

void VSTPluginInstance::configureVST3Buses(int requestedChannelCount)
{
    if(!vst3Component || !vst3Processor || vst3Active) return;
    processingSetupValid=true;
    vst3InputBusCount=(std::max)(0,vst3Component->getBusCount(kAudio,kInput));
    vst3OutputBusCount=(std::max)(0,vst3Component->getBusCount(kAudio,kOutput));
    std::vector<SpeakerArrangement> inputs(vst3InputBusCount),outputs(vst3OutputBusCount);
    auto collect=[&](BusDirection direction,std::vector<SpeakerArrangement>& arrangements,std::vector<BusInfo>& info,int& mainIndex) {
        mainIndex=-1;
        info.resize(arrangements.size());
        for(int32 i=0;i<int32(arrangements.size());++i) {
            info[i]={}; vst3Component->getBusInfo(kAudio,direction,i,info[i]);
            vst3Processor->getBusArrangement(direction,i,arrangements[i]);
            if(mainIndex<0 && info[i].busType==kMain)mainIndex=i;
            // All buses remain discoverable; auxiliary routing is not invented.
            vst3Component->activateBus(kAudio,direction,i,i==mainIndex);
        }
        for(int32 i=0;i<vst3Component->getBusCount(kEvent,direction);++i)
            vst3Component->activateBus(kEvent,direction,i,false);
    };
    collect(kInput,inputs,vst3InputBusInfo,vst3InputMainIndex); collect(kOutput,outputs,vst3OutputBusInfo,vst3OutputMainIndex);
    const auto originalInputs=inputs,originalOutputs=outputs;
    const auto requested=speakerArrangementForChannelCount(requestedChannelCount);
    using Layout=std::pair<std::vector<SpeakerArrangement>,std::vector<SpeakerArrangement>>;
    std::vector<Layout> attempted;
    const auto negotiate=[&](bool adaptInput,bool adaptOutput) {
        inputs=originalInputs;outputs=originalOutputs;
        if(requested!=SpeakerArr::kEmpty) {
            if(adaptInput && vst3InputMainIndex>=0)inputs[vst3InputMainIndex]=requested;
            if(adaptOutput && vst3OutputMainIndex>=0)outputs[vst3OutputMainIndex]=requested;
        }
        const Layout layout{inputs,outputs};
        if(std::find(attempted.begin(),attempted.end(),layout)!=attempted.end())return false;
        attempted.push_back(layout);
        return vst3Processor->setBusArrangements(inputs.data(),int32(inputs.size()),outputs.data(),int32(outputs.size()))==kResultOk;
    };
    const bool asymmetric=vst3InputMainIndex>=0 && vst3OutputMainIndex>=0
        && SpeakerArr::getChannelCount(originalInputs[vst3InputMainIndex])!=SpeakerArr::getChannelCount(originalOutputs[vst3OutputMainIndex]);
    // Negotiate the main directions independently. Each rejected request starts
    // again from the announced buses; auxiliary layouts stay untouched.
    if(asymmetric) {
        const bool outputMatches=vst3OutputMainIndex>=0 && originalOutputs[vst3OutputMainIndex]==requested;
        processingSetupValid=(outputMatches ? negotiate(true,false)||negotiate(false,true)
                                            : negotiate(false,true)||negotiate(true,false))
            ||negotiate(true,true)||negotiate(false,false);
    } else {
        processingSetupValid=negotiate(true,true)||negotiate(false,true)
            ||negotiate(true,false)||negotiate(false,false);
    }
    BusInfo info{};
    vst3InputChannelCount=0; vst3OutputChannelCount=0;
    if(vst3InputMainIndex>=0 && vst3Component->getBusInfo(kAudio,kInput,vst3InputMainIndex,info)==kResultOk) vst3InputChannelCount=info.channelCount;
    if(vst3OutputMainIndex>=0 && vst3Component->getBusInfo(kAudio,kOutput,vst3OutputMainIndex,info)==kResultOk) vst3OutputChannelCount=info.channelCount;
    vst3InputBuffers.resize(inputs.size()); vst3OutputBuffers.resize(outputs.size());
}

SpeakerArrangement VSTPluginInstance::speakerArrangementForChannelCount(int count) const
{
	switch (count)
	{
	case 1:
		return SpeakerArr::kMono;
	case 2:
		return SpeakerArr::kStereo;
	case 4:
		return SpeakerArr::k40Music;
	case 5:
		return SpeakerArr::k50;
	case 6:
		return SpeakerArr::k51;
	case 7:
		return SpeakerArr::k61Cine;
	case 8:
		return SpeakerArr::k71Music;
	default:
		return SpeakerArr::kEmpty;
	}
}

void VSTPluginInstance::setEditorScale(float scale) {
    if(!vst3View || scale<=0 || scale==editorScale) return;
    editorScale=scale;
    IPlugViewContentScaleSupport* support=nullptr;
    if(vst3View->queryInterface(IPlugViewContentScaleSupport::iid,reinterpret_cast<void**>(&support))==kResultOk) {
        support->setContentScaleFactor(scale); support->release();
    }
}
void VSTPluginInstance::serviceControllerParameterChanges() {
    if(effect && legacyIdleRequested.load(std::memory_order_acquire))
        effect->control(effect,VST_EFFECT_OPCODE_IDLE,0,0,nullptr,0);
    // Controller owner only; independent of the processor's realtime ownership.
    latestControllerInputs.drain(256,[&](ParamID id,double value) noexcept {
        if(vst3Controller)vst3Controller->setParamNormalized(id,value);
        return true;
    });
    EqApoRT::ParameterEvent change{};
    while(outputVST3ParameterChanges.pop(change)) if(vst3Controller) vst3Controller->setParamNormalized(change.id,change.value);
    latestOutputParameters.drain(256,[&](ParamID id,double value) noexcept {
        if(vst3Controller)vst3Controller->setParamNormalized(id,value);
        return true;
    });
    outputMailboxPending.store(latestOutputParameters.hasDirty(),std::memory_order_release);
}
bool VSTPluginInstance::serviceRestartRequests() {
    std::lock_guard<std::recursive_mutex> mirrorLock(mirrorMutex);
    // Structural changes need exclusive component ownership at a boundary.
    const auto flags=restartFlags.exchange(0,std::memory_order_acq_rel);
    serviceControllerParameterChanges();
    if(!flags || !vst3Processor) return true;
    wchar_t restartAudit[2]{};
    if(GetEnvironmentVariableW(L"EQAPO_RESTART_AUDIT",restartAudit,2)){TraceF(L"VST3 control restart flags=%u view=%u",flags,vst3View?1:0);}
    if(flags & kReloadComponent) {
        // Structural reload is owned by the control path, never by the APO callback.
        std::wstring savedState; std::unordered_map<std::wstring,float> savedParameters;
        readFromEffect(savedState,savedParameters);
        const bool wasProcessing=vst3Processing;
        const auto rate=sampleRate;const auto block=preparedBlockSize;const auto channels=usedChannelCount;
        stopProcessing();releaseVST3();
        if(!initializeVST3()) {processingSetupValid=false;return false;}
        setUsedChannelCount(channels);writeToEffect(savedState,savedParameters);
        prepareForProcessing(rate,block,preferDoubleProcessing);
        if(wasProcessing) startProcessing();
        stateGeneration.fetch_add(1,std::memory_order_release);
        return processingSetupValid;
    }
    const bool resume=vst3Processing;
    if(flags & (kIoChanged|kLatencyChanged)) {
        stopProcessing(); prepareForProcessing(sampleRate,preparedBlockSize,preferDoubleProcessing);
        if(resume) startProcessing();
    }
    if(flags & kParamValuesChanged) {
        bool changed=false;
        if(vst3Controller) for(int32 i=0;i<vst3Controller->getParameterCount();++i) {
            ParameterInfo info{};
            if(vst3Controller->getParameterInfo(i,info)==kResultOk && !(info.flags & ParameterInfo::kIsReadOnly)) {
                const auto value=vst3Controller->getParamNormalized(info.id);
                const auto previous=notifiedParameterValues.find(info.id);
                if(previous!=notifiedParameterValues.end() && previous->second==value)continue;
                if(!routeEditsThroughSink)queueVST3ParameterChange(info.id,value);
                notifyParameterEdit(info.id,value);
                notifiedParameterValues[info.id]=value;
                changed=true;
            }
        }
        if(changed)onAutomate();
    }
    stateGeneration.fetch_add(1,std::memory_order_release);
    return processingSetupValid;
}
