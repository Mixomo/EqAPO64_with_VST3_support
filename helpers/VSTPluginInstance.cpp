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
#include "../Version.h"
#include "VSTPluginLibrary.h"
#include "VSTPluginInstance.h"
#include "pluginterfaces/base/futils.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

#define equalizerApoVSTID VST_FOURCC('E', 'A', 'P', 'O');

vst_time_info vstTime{ 0,0,0,0,0,0,0,0,0,0,{0}, 0xFFFF };

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
		if (numBytes <= 0)
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
static const char vst3StateEnvelopeMagic[] = "EAPOVST3STATE2\n";
static const wchar_t vst3ParamIdPrefix[] = L"#";

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

static vector<char> makeVST3Envelope(const vector<char>& componentState, const vector<char>& controllerState, const unordered_map<wstring, float>& paramMap)
{
	vector<char> data;
	appendBytes(data, vst3StateEnvelopeMagic, strlen(vst3StateEnvelopeMagic));
	appendBlob(data, componentState);
	appendBlob(data, controllerState);
	appendParamMap(data, paramMap);
	return data;
}

static bool parseVST3Envelope(const vector<char>& data, vector<char>& componentState, vector<char>& controllerState, unordered_map<wstring, float>& paramMap)
{
	if (!hasEnvelopeMagic(data, vst3StateEnvelopeMagic))
		return false;
	size_t offset = strlen(vst3StateEnvelopeMagic);
	return readBlob(data, offset, componentState) && readBlob(data, offset, controllerState) && readParamMap(data, offset, paramMap);
}

class VSTPluginInstance::VST3HostContext : public IHostApplication, public IComponentHandler, public IPlugFrame
{
public:
	VST3HostContext(VSTPluginInstance* instance) : instance(instance) {}

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		QUERY_INTERFACE(iid, obj, FUnknown::iid, IHostApplication)
		QUERY_INTERFACE(iid, obj, IHostApplication::iid, IHostApplication)
		QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
		QUERY_INTERFACE(iid, obj, IPlugFrame::iid, IPlugFrame)
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

	tresult PLUGIN_API createInstance(TUID, TUID, void**) override
	{
		return kNoInterface;
	}

	tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
	tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
	{
		if (instance != NULL)
		{
			instance->queueVST3ParameterChange(id, value);
			instance->onAutomate();
		}
		return kResultOk;
	}
	tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
	tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }

	tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override
	{
		if (view != NULL && newSize != NULL)
		{
			view->onSize(newSize);
			if (instance != NULL)
				instance->onSizeWindow(newSize->getWidth(), newSize->getHeight());
		}
		return kResultOk;
	}

private:
	volatile LONG refCount = 1;
	VSTPluginInstance* instance;
};

static intptr_t callback(struct vst_effect_t* effect, int32_t opcode, int32_t index, int64_t value, const char* ptr, float opt)
{
	VSTPluginInstance* instance = effect != NULL ? (VSTPluginInstance*)effect->host_internal : NULL;
#ifdef _DEBUG
	printf("vst: %p opcode: %d index: %d value: %" PRIdPTR " ptr: %p opt: %f host_internal: %p\n",
		effect, opcode, index, value, ptr, opt, effect != NULL ? effect->host_internal : NULL);
	fflush(stdout);
#endif

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
		return effect != NULL ? effect->control(effect, VST_HOST_OPCODE_KEEPALIVE_OR_IDLE, 0, 0, NULL, 0.0f) : 0;

	case VST_HOST_OPCODE_EDITOR_UPDATE:
		return effect != NULL ? effect->control(effect, VST_EFFECT_OPCODE_EDITOR_KEEP_ALIVE, 0, 0, NULL, 0.0f) : 0;

	case VST_HOST_OPCODE_GET_TIME:
		if (instance != NULL) {
			vstTime.sampleRate = instance->getSampleRate();
			return (intptr_t)&vstTime;
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
			char* s = (char*)ptr;
#ifdef _DEBUG
			printf("VST canDo: %s\n", s);
			fflush(stdout);
#endif
			if (strcmp(s, "startStopProcess") == 0 ||
				strcmp(s, "sizeWindow") == 0)
				return 1;
		}
		return 0;

	case VST_HOST_OPCODE_AUTOMATE:
		if (instance != NULL)
			instance->onAutomate();
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

VSTPluginInstance::VSTPluginInstance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel, int vst3ClassIndex)
	: library(library), processLevel(processLevel), vst3ClassIndex(vst3ClassIndex)
{
}

VSTPluginInstance::~VSTPluginInstance()
{
	automateFunc = nullptr;
	sizeWindowFunc = nullptr;
	releaseVST3();
	if (effect != NULL)
	{
		effect->control(effect, VST_EFFECT_OPCODE_DESTROY, 0, 0, NULL, 0.0f);
		effect = NULL;
	}
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

bool VSTPluginInstance::initializeVST3()
{
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

	TUID controllerClassId;
	memset(controllerClassId, 0, sizeof(controllerClassId));
	if (vst3Component->getControllerClassId(controllerClassId) == kResultOk)
	{
		TUID controllerIid;
		IEditController::iid.toTUID(controllerIid);
		library->getFactory()->createInstance(controllerClassId, controllerIid, (void**)&vst3Controller);
	}
	if (vst3Controller == NULL)
	{
		TUID controllerIid;
		IEditController::iid.toTUID(controllerIid);
		vst3Component->queryInterface(controllerIid, (void**)&vst3Controller);
	}
	if (vst3Controller != NULL)
	{
		vst3Controller->initialize(static_cast<IHostApplication*>(vst3HostContext));
		vst3Controller->setComponentHandler(vst3HostContext);

		VST3MemoryStream* stream = new VST3MemoryStream();
		if (vst3Component->getState(stream) == kResultOk)
		{
			stream->seek(0, IBStream::kIBSeekSet);
			vst3Controller->setComponentState(stream);
		}
		stream->release();

		TUID connectionPointIid;
		Steinberg::Vst::IConnectionPoint::iid.toTUID(connectionPointIid);
		if (vst3Component->queryInterface(connectionPointIid, (void**)&vst3ComponentConnection) == kResultOk
			&& vst3Controller->queryInterface(connectionPointIid, (void**)&vst3ControllerConnection) == kResultOk)
		{
			vst3ComponentConnection->connect(vst3ControllerConnection);
			vst3ControllerConnection->connect(vst3ComponentConnection);
		}
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
		return vst3SupportsDouble;
	return (effect->flags & VST_EFFECT_FLAG_SUPPORTS_DOUBLE) != 0;
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

void VSTPluginInstance::prepareForProcessing(float sampleRate, int blockSize)
{
	if (library->isVST3())
	{
		if (vst3Processor == NULL || vst3Component == NULL)
			return;

		this->sampleRate = sampleRate;
		configureVST3Buses(usedChannelCount > 0 ? usedChannelCount : max(vst3InputChannelCount, vst3OutputChannelCount));
		ProcessSetup setup;
		setup.processMode = kRealtime;
		setup.symbolicSampleSize = vst3SupportsDouble ? kSample64 : kSample32;
		setup.maxSamplesPerBlock = blockSize;
		setup.sampleRate = sampleRate;
		vst3Processor->setupProcessing(setup);
		vst3SamplePosition = 0;
		return;
	}

	if (effect == NULL)
		return;

	this->sampleRate = sampleRate;
	effect->control(effect, VST_EFFECT_OPCODE_SET_SAMPLE_RATE, 0, 0, NULL, sampleRate);
	effect->control(effect, VST_EFFECT_OPCODE_SET_BLOCK_SIZE, 0, blockSize, NULL, 0.0f);
}

void VSTPluginInstance::writeToEffect(const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap)
{
	if (library->isVST3())
	{
		if (chunkData != L"")
		{
			vector<char> data;
			if (base64Decode(chunkData, data))
			{
				vector<char> componentState;
				vector<char> controllerState;
				unordered_map<wstring, float> envelopeParamMap;
				const bool isEnvelope = parseVST3Envelope(data, componentState, controllerState, envelopeParamMap);
				if (!isEnvelope)
					componentState = data;

				if (!componentState.empty())
				{
					VST3MemoryStream* stream = new VST3MemoryStream(componentState);
					if (vst3Component != NULL)
						vst3Component->setState(stream);
					stream->seek(0, IBStream::kIBSeekSet);
					if (vst3Controller != NULL)
						vst3Controller->setComponentState(stream);
					if (!isEnvelope && vst3Controller != NULL)
					{
						stream->seek(0, IBStream::kIBSeekSet);
						vst3Controller->setState(stream);
					}
					stream->release();
				}

				if (!controllerState.empty() && vst3Controller != NULL)
				{
					VST3MemoryStream* stream = new VST3MemoryStream(controllerState);
					vst3Controller->setState(stream);
					stream->release();
				}
				if (!controllerState.empty() && vst3Component != NULL)
				{
					VST3MemoryStream* stream = new VST3MemoryStream(controllerState);
					vst3Component->setState(stream);
					stream->release();
				}

				const unordered_map<wstring, float>& paramsToApply = isEnvelope ? envelopeParamMap : paramMap;
				if (vst3Controller != NULL)
				{
					for (auto it : paramsToApply)
					{
						if (!it.first.empty() && it.first[0] == vst3ParamIdPrefix[0])
						{
							wchar_t* end = NULL;
							ParamID id = static_cast<ParamID>(wcstoul(it.first.c_str() + 1, &end, 10));
							if (end != it.first.c_str() + 1)
							{
								vst3Controller->setParamNormalized(id, it.second);
								queueVST3ParameterChange(id, it.second);
							}
							continue;
						}
						for (int32 i = 0; i < vst3Controller->getParameterCount(); i++)
						{
							ParameterInfo info;
							if (vst3Controller->getParameterInfo(i, info) == kResultOk
								&& it.first == wstring((wchar_t*)info.title))
							{
								vst3Controller->setParamNormalized(info.id, it.second);
								queueVST3ParameterChange(info.id, it.second);
								break;
							}
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
					wchar_t* end = NULL;
					ParamID id = static_cast<ParamID>(wcstoul(it.first.c_str() + 1, &end, 10));
					if (end != it.first.c_str() + 1)
					{
						vst3Controller->setParamNormalized(id, it.second);
						queueVST3ParameterChange(id, it.second);
					}
					continue;
				}
				for (int32 i = 0; i < vst3Controller->getParameterCount(); i++)
				{
					ParameterInfo info;
					if (vst3Controller->getParameterInfo(i, info) == kResultOk
						&& it.first == wstring((wchar_t*)info.title))
					{
						vst3Controller->setParamNormalized(info.id, it.second);
						queueVST3ParameterChange(info.id, it.second);
						break;
					}
				}
			}
		}
		return;
	}

	if (effect == NULL)
		return;

	bool appliedChunk = false;
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
					appliedChunk = true;
				}

				if (isEnvelope)
				{
					for (int i = 0; i < effect->num_params; i++)
					{
						char buf[256];
						effect->control(effect, VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
						buf[255] = '\0';
						auto it = envelopeParamMap.find(StringHelper::toWString(buf, CP_UTF8));
						if (it != envelopeParamMap.end())
							effect->set_parameter(effect, i, it->second);
					}
				}
			}
		}
	}
	if (!appliedChunk)
	{
		for (int i = 0; i < effect->num_params; i++)
		{
			char buf[256];
			effect->control(effect, VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
			buf[255] = '\0'; // just to be sure
			wstring name = StringHelper::toWString(buf, CP_UTF8);
			auto it = paramMap.find(name);
			if (it != paramMap.end())
				effect->set_parameter(effect, i, it->second);
		}
	}
}

void VSTPluginInstance::readFromEffect(std::wstring& chunkData, std::unordered_map<std::wstring, float>& paramMap) const
{
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
				if (vst3Controller->getParameterInfo(i, info) == kResultOk)
				{
					paramMap[wstring((wchar_t*)info.title)] = (float)vst3Controller->getParamNormalized(info.id);
					paramMap[wstring(vst3ParamIdPrefix) + to_wstring(info.id)] = (float)vst3Controller->getParamNormalized(info.id);
				}
			}
		}

		if (!componentState.empty() || !controllerState.empty())
		{
			vector<char> envelope = makeVST3Envelope(componentState, controllerState, paramMap);
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
			paramMap[StringHelper::toWString(buf, CP_UTF8)] = value;
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
			paramMap[StringHelper::toWString(buf, CP_UTF8)] = value;
		}
	}
}

void VSTPluginInstance::startProcessing()
{
	if (library->isVST3())
	{
		if (vst3Component != NULL && !vst3Active)
		{
			vst3Component->setActive(true);
			vst3Active = true;
		}
		if (vst3Processor != NULL && !vst3Processing)
		{
			vst3Processor->setProcessing(true);
			vst3Processing = true;
		}
		return;
	}

	if (effect == NULL)
		return;

	effect->control(effect, VST_EFFECT_OPCODE_SUSPEND, 0, 1, NULL, 0.0f);
	effect->control(effect, VST_EFFECT_OPCODE_PROCESS_BEGIN, 0, 0, NULL, 0.0f);
}

void VSTPluginInstance::processReplacing(float** inputArray, float** outputArray, int frameCount)
{
	if (library->isVST3())
	{
		if (vst3Processor == NULL)
			return;
		AudioBusBuffers inputBuffers;
		inputBuffers.numChannels = numInputs();
		inputBuffers.channelBuffers32 = inputArray;
		AudioBusBuffers outputBuffers;
		outputBuffers.numChannels = numOutputs();
		outputBuffers.channelBuffers32 = outputArray;
		ProcessData data;
		data.processMode = kRealtime;
		data.symbolicSampleSize = kSample32;
		data.numSamples = frameCount;
		data.numInputs = vst3InputBusCount > 0 ? 1 : 0;
		data.numOutputs = vst3OutputBusCount > 0 ? 1 : 0;
		data.inputs = data.numInputs > 0 ? &inputBuffers : NULL;
		data.outputs = data.numOutputs > 0 ? &outputBuffers : NULL;
		ParameterChanges inputChanges(static_cast<int32>(pendingVST3ParameterChanges.size()));
		for (const auto& change : pendingVST3ParameterChanges)
		{
			int32 queueIndex = -1;
			IParamValueQueue* queue = inputChanges.addParameterData(change.first, queueIndex);
			if (queue != NULL)
			{
				int32 pointIndex = -1;
				queue->addPoint(0, change.second, pointIndex);
			}
		}
		data.inputParameterChanges = pendingVST3ParameterChanges.empty() ? static_cast<IParameterChanges*>(&emptyVST3ParameterChanges) : static_cast<IParameterChanges*>(&inputChanges);
		data.inputEvents = &emptyVST3EventList;
		data.processContext = &vst3ProcessContext;
		vst3ProcessContext.state = ProcessContext::kPlaying | ProcessContext::kContTimeValid;
		vst3ProcessContext.sampleRate = sampleRate;
		vst3ProcessContext.projectTimeSamples = vst3SamplePosition;
		vst3ProcessContext.continousTimeSamples = vst3SamplePosition;
		if (vst3Processor->process(data) == kResultOk)
			vst3SamplePosition += frameCount;
		pendingVST3ParameterChanges.clear();
		return;
	}

	if (effect == NULL)
		return;

	effect->process_float(effect, inputArray, outputArray, frameCount);
}

void VSTPluginInstance::processDoubleReplacing(double** inputArray, double** outputArray, int frameCount)
{
	if (library->isVST3())
	{
		if (vst3Processor == NULL)
			return;
		AudioBusBuffers inputBuffers;
		inputBuffers.numChannels = numInputs();
		inputBuffers.channelBuffers64 = inputArray;
		AudioBusBuffers outputBuffers;
		outputBuffers.numChannels = numOutputs();
		outputBuffers.channelBuffers64 = outputArray;
		ProcessData data;
		data.processMode = kRealtime;
		data.symbolicSampleSize = kSample64;
		data.numSamples = frameCount;
		data.numInputs = vst3InputBusCount > 0 ? 1 : 0;
		data.numOutputs = vst3OutputBusCount > 0 ? 1 : 0;
		data.inputs = data.numInputs > 0 ? &inputBuffers : NULL;
		data.outputs = data.numOutputs > 0 ? &outputBuffers : NULL;
		ParameterChanges inputChanges(static_cast<int32>(pendingVST3ParameterChanges.size()));
		for (const auto& change : pendingVST3ParameterChanges)
		{
			int32 queueIndex = -1;
			IParamValueQueue* queue = inputChanges.addParameterData(change.first, queueIndex);
			if (queue != NULL)
			{
				int32 pointIndex = -1;
				queue->addPoint(0, change.second, pointIndex);
			}
		}
		data.inputParameterChanges = pendingVST3ParameterChanges.empty() ? static_cast<IParameterChanges*>(&emptyVST3ParameterChanges) : static_cast<IParameterChanges*>(&inputChanges);
		data.inputEvents = &emptyVST3EventList;
		data.processContext = &vst3ProcessContext;
		vst3ProcessContext.state = ProcessContext::kPlaying | ProcessContext::kContTimeValid;
		vst3ProcessContext.sampleRate = sampleRate;
		vst3ProcessContext.projectTimeSamples = vst3SamplePosition;
		vst3ProcessContext.continousTimeSamples = vst3SamplePosition;
		if (vst3Processor->process(data) == kResultOk)
			vst3SamplePosition += frameCount;
		pendingVST3ParameterChanges.clear();
		return;
	}

	if (effect == NULL)
		return;

	effect->process_double(effect, inputArray, outputArray, frameCount);
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
	effect->control(effect, VST_EFFECT_OPCODE_SUSPEND, 0, 0, NULL, 0.0f);
}

void VSTPluginInstance::queueVST3ParameterChange(ParamID id, ParamValue value)
{
	for (auto& change : pendingVST3ParameterChanges)
	{
		if (change.first == id)
		{
			change.second = value;
			return;
		}
	}
	pendingVST3ParameterChanges.push_back(make_pair(id, value));
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
		return true;
	}

	if (effect == NULL)
		return false;

	vst_rect_t* rect;
	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_GET_RECT, 0, 0, &rect, 0.0f);
	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_OPEN, 0, 0, hWnd, 0.0f);
	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_GET_RECT, 0, 0, &rect, 0.0f);

	if (width != NULL)
		*width = rect->right - rect->left;
	if (height != NULL)
		*height = rect->bottom - rect->top;
	return true;
}

void VSTPluginInstance::doIdle()
{
	if (library->isVST3())
	{
		if (vst3EditorHostWindow != NULL)
		{
			InvalidateRect(vst3EditorHostWindow, NULL, FALSE);
			UpdateWindow(vst3EditorHostWindow);
			EnumChildWindows(vst3EditorHostWindow, [](HWND hWnd, LPARAM) -> BOOL {
				InvalidateRect(hWnd, NULL, FALSE);
				UpdateWindow(hWnd);
				return TRUE;
			}, 0);
		}
		return;
	}

	if (effect == NULL)
		return;

	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_KEEP_ALIVE, 0, 0, NULL, 0.0f);
}

void VSTPluginInstance::stopEditing()
{
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

void VSTPluginInstance::onAutomate()
{
	if (automateFunc)
		automateFunc();
}

void VSTPluginInstance::setSizeWindowFunc(std::function<void(int, int)> func)
{
	sizeWindowFunc = func;
}

bool VSTPluginInstance::getEditorSize(int* width, int* height)
{
	(void)width;
	(void)height;
	return false;
}

void VSTPluginInstance::onSizeWindow(int w, int h)
{
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
		vst3Controller->terminate();
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
	if (vst3Component == NULL || vst3Processor == NULL)
		return;

	int channelCount = max(1, requestedChannelCount);
	SpeakerArrangement inputArrangement = speakerArrangementForChannelCount(channelCount);
	SpeakerArrangement outputArrangement = speakerArrangementForChannelCount(channelCount);

	if (inputArrangement == SpeakerArr::kEmpty || outputArrangement == SpeakerArr::kEmpty)
	{
		BusInfo busInfo;
		memset(&busInfo, 0, sizeof(busInfo));
		if (vst3InputBusCount > 0 && vst3Component->getBusInfo(kAudio, kInput, 0, busInfo) == kResultOk && busInfo.channelCount > 0)
			inputArrangement = speakerArrangementForChannelCount(busInfo.channelCount);
		if (vst3OutputBusCount > 0 && vst3Component->getBusInfo(kAudio, kOutput, 0, busInfo) == kResultOk && busInfo.channelCount > 0)
			outputArrangement = speakerArrangementForChannelCount(busInfo.channelCount);
	}

	for (int i = 0; i < vst3InputBusCount; i++)
		vst3Component->activateBus(kAudio, kInput, i, i == 0);
	for (int i = 0; i < vst3OutputBusCount; i++)
		vst3Component->activateBus(kAudio, kOutput, i, i == 0);

	if (inputArrangement != SpeakerArr::kEmpty || outputArrangement != SpeakerArr::kEmpty)
	{
		vst3Processor->setBusArrangements(
			vst3InputBusCount > 0 && inputArrangement != SpeakerArr::kEmpty ? &inputArrangement : NULL,
			vst3InputBusCount > 0 && inputArrangement != SpeakerArr::kEmpty ? 1 : 0,
			vst3OutputBusCount > 0 && outputArrangement != SpeakerArr::kEmpty ? &outputArrangement : NULL,
			vst3OutputBusCount > 0 && outputArrangement != SpeakerArr::kEmpty ? 1 : 0);
	}

	BusInfo busInfo;
	memset(&busInfo, 0, sizeof(busInfo));
	if (vst3InputBusCount > 0 && vst3Component->getBusInfo(kAudio, kInput, 0, busInfo) == kResultOk)
		vst3InputChannelCount = max(0, busInfo.channelCount);
	if (vst3OutputBusCount > 0 && vst3Component->getBusInfo(kAudio, kOutput, 0, busInfo) == kResultOk)
		vst3OutputChannelCount = max(0, busInfo.channelCount);
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
