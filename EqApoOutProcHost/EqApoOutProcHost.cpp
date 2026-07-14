#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <shellapi.h>

#include <cmath>
#include <cstdint>
#include <cwchar>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "helpers/VSTPluginInstance.h"
#include "helpers/VSTPluginLibrary.h"
#include "outproc/OutProcAudioProtocol.h"
#include "outproc/OutProcVSTConfig.h"

static HANDLE parseHandle(int argc, wchar_t** argv, const wchar_t* name)
{
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (wcscmp(argv[i], name) == 0)
			return reinterpret_cast<HANDLE>(_wcstoui64(argv[i + 1], nullptr, 10));
	}
	return NULL;
}

static std::wstring parseStringArg(int argc, wchar_t** argv, const wchar_t* name)
{
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (wcscmp(argv[i], name) == 0)
			return argv[i + 1];
	}
	return L"";
}

static bool hasFlag(int argc, wchar_t** argv, const wchar_t* name)
{
	for (int i = 1; i < argc; ++i)
	{
		if (wcscmp(argv[i], name) == 0)
			return true;
	}
	return false;
}

static void convertFloatToDouble(double* dest, const float* src, std::size_t count)
{
	for (std::size_t i = 0; i < count; ++i)
		dest[i] = src[i];
}

static void convertDoubleToFloat(float* dest, const double* src, std::size_t count)
{
	for (std::size_t i = 0; i < count; ++i)
		dest[i] = static_cast<float>(src[i]);
}

struct VstRuntime
{
	OutProcVSTConfig config;
	std::shared_ptr<VSTPluginLibrary> library;
	std::vector<std::unique_ptr<VSTPluginInstance>> effects;
	std::vector<std::vector<double>> emptyChannels;
	std::vector<double*> inputArray;
	std::vector<double*> outputArray;
	std::vector<std::vector<float>> floatInputStorage;
	std::vector<std::vector<float>> floatOutputStorage;
	std::vector<float*> floatInputs;
	std::vector<float*> floatOutputs;
	std::uint32_t channelCount = 0;
	std::uint32_t maxFrames = 0;
	std::uint32_t effectChannelCount = 0;
	bool usesGuiEffect = false;
	bool initialized = false;
	bool failed = false;
};

struct HostDspState
{
	bool initialized = false;
	double currentGain = 1.0;
	std::uint32_t channelCount = 0;
	double a0 = 1.0;
	double a1 = 0.0;
	double a2 = 0.0;
	double b1 = 0.0;
	double b2 = 0.0;
	std::vector<double> x1;
	std::vector<double> x2;
	std::vector<double> y1;
	std::vector<double> y2;
	std::wstring vstConfigPath;
	VstRuntime vst;
};

static bool processBlock(OutProcAudioHeader* header, HostDspState& state);

static const wchar_t* guiWindowClassName = L"EqApoOutProcVSTGuiWindow";
static const wchar_t* guiChildWindowClassName = L"EqApoOutProcVSTGuiChild";
static const SIZE_T guiPrivateBytesLimit = static_cast<SIZE_T>(1536) * 1024 * 1024;
static VSTPluginInstance* guiEffect = nullptr;
static HWND guiWindow = NULL;
static HWND guiChild = NULL;
static HANDLE guiMapping = NULL;
static HANDLE guiRequest = NULL;
static HANDLE guiResponse = NULL;
static HANDLE guiShutdown = NULL;
static HANDLE guiShowEvent = NULL;
static HANDLE guiHideEvent = NULL;
static HANDLE guiHiddenEvent = NULL;
static HANDLE guiExitEvent = NULL;
static HANDLE guiInfoMapping = NULL;
static OutProcAudioHeader* guiHeader = nullptr;
static std::wstring guiVstConfigPath;
static ULONGLONG guiLastConfigWriteTick = 0;
static CRITICAL_SECTION guiEffectLock;
static bool guiEffectLockInitialized = false;

struct GuiAudioThreadContext
{
	std::wstring sessionId;
	HostDspState* dspState = nullptr;
	HANDLE stopEvent = NULL;
};

static void lockGuiEffect()
{
	if (guiEffectLockInitialized)
		EnterCriticalSection(&guiEffectLock);
}

static void unlockGuiEffect()
{
	if (guiEffectLockInitialized)
		LeaveCriticalSection(&guiEffectLock);
}

static bool guiMemoryLimitExceeded()
{
	PROCESS_MEMORY_COUNTERS_EX counters = {};
	counters.cb = sizeof(counters);
	if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
		return false;
	return counters.PrivateUsage > guiPrivateBytesLimit;
}

static std::wstring makeSessionObjectName(const std::wstring& sessionId, const wchar_t* suffix)
{
	std::wstring safeId = sessionId;
	for (wchar_t& ch : safeId)
	{
		const bool ok = (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || ch == L'-' || ch == L'_';
		if (!ok)
			ch = L'_';
	}
	return L"Global\\EqApoOutProcVST_" + safeId + L"_" + suffix;
}

static std::wstring getTempDirectory()
{
	wchar_t tempPath[MAX_PATH] = {};
	const DWORD length = GetTempPathW(MAX_PATH, tempPath);
	if (length == 0 || length >= MAX_PATH)
		return L"";
	return std::wstring(tempPath);
}

static void appendDebugLog(const std::wstring& message)
{
	const std::wstring tempDirectory = getTempDirectory();
	if (tempDirectory.empty())
		return;

	std::wofstream stream(tempDirectory + L"EqApoOutProcHost-debug.log", std::ios::app);
	if (stream)
		stream << L"[host pid=" << GetCurrentProcessId() << L"] " << message << std::endl;
}

static std::wstring makeSafeSessionId(const std::wstring& sessionId)
{
	std::wstring safeId = sessionId;
	for (wchar_t& ch : safeId)
	{
		const bool ok = (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || ch == L'-' || ch == L'_';
		if (!ok)
			ch = L'_';
	}
	return safeId;
}

static std::wstring makeSessionPidPath(const std::wstring& sessionId)
{
	const std::wstring tempDirectory = getTempDirectory();
	if (tempDirectory.empty())
		return L"";
	return tempDirectory + L"EqApoOutProcHost-" + makeSafeSessionId(sessionId) + L".pid";
}

static void writeSessionPidFile(const std::wstring& sessionId)
{
	const std::wstring path = makeSessionPidPath(sessionId);
	if (path.empty())
		return;

	std::wofstream stream(path, std::ios::trunc);
	if (stream)
	{
		stream << GetCurrentProcessId();
		appendDebugLog(L"wrote pid file session=" + sessionId + L" path=" + path);
	}
	else
		appendDebugLog(L"failed to write pid file session=" + sessionId + L" path=" + path);
}

static void deleteSessionPidFile(const std::wstring& sessionId)
{
	const std::wstring path = makeSessionPidPath(sessionId);
	if (!path.empty())
	{
		DeleteFileW(path.c_str());
		appendDebugLog(L"deleted pid file session=" + sessionId + L" path=" + path);
	}
}

static bool ensureGuiSessionConnected(const std::wstring& sessionId)
{
	if (sessionId.empty())
		return false;
	if (guiHeader != nullptr && guiRequest != NULL && guiResponse != NULL && guiShutdown != NULL)
		return true;

	guiMapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, makeSessionObjectName(sessionId, L"Map").c_str());
	guiRequest = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, makeSessionObjectName(sessionId, L"Request").c_str());
	guiResponse = OpenEventW(EVENT_MODIFY_STATE, FALSE, makeSessionObjectName(sessionId, L"Response").c_str());
	guiShutdown = OpenEventW(SYNCHRONIZE, FALSE, makeSessionObjectName(sessionId, L"Shutdown").c_str());

	if (guiMapping == NULL || guiRequest == NULL || guiResponse == NULL || guiShutdown == NULL)
	{
		if (guiMapping != NULL)
			CloseHandle(guiMapping);
		if (guiRequest != NULL)
			CloseHandle(guiRequest);
		if (guiResponse != NULL)
			CloseHandle(guiResponse);
		if (guiShutdown != NULL)
			CloseHandle(guiShutdown);
		guiMapping = guiRequest = guiResponse = guiShutdown = NULL;
		return false;
	}

	guiHeader = static_cast<OutProcAudioHeader*>(MapViewOfFile(guiMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
	if (guiHeader == nullptr)
	{
		CloseHandle(guiMapping);
		CloseHandle(guiRequest);
		CloseHandle(guiResponse);
		CloseHandle(guiShutdown);
		guiMapping = guiRequest = guiResponse = guiShutdown = NULL;
		return false;
	}
	return true;
}

static void closeGuiSession()
{
	if (guiHeader != nullptr)
		UnmapViewOfFile(guiHeader);
	if (guiMapping != NULL)
		CloseHandle(guiMapping);
	if (guiRequest != NULL)
		CloseHandle(guiRequest);
	if (guiResponse != NULL)
		CloseHandle(guiResponse);
	if (guiShutdown != NULL)
		CloseHandle(guiShutdown);
	if (guiShowEvent != NULL)
		CloseHandle(guiShowEvent);
	if (guiHideEvent != NULL)
		CloseHandle(guiHideEvent);
	if (guiHiddenEvent != NULL)
		CloseHandle(guiHiddenEvent);
	if (guiExitEvent != NULL)
		CloseHandle(guiExitEvent);
	if (guiInfoMapping != NULL)
		CloseHandle(guiInfoMapping);
	guiHeader = nullptr;
	guiMapping = guiRequest = guiResponse = guiShutdown = NULL;
	guiShowEvent = guiHideEvent = guiHiddenEvent = guiExitEvent = NULL;
	guiInfoMapping = NULL;
}

static DWORD WINAPI guiAudioThreadProc(LPVOID param)
{
	GuiAudioThreadContext* context = static_cast<GuiAudioThreadContext*>(param);
	if (context == nullptr || context->dspState == nullptr || context->stopEvent == NULL)
		return 1;

	bool running = true;
	while (running)
	{
		if (!ensureGuiSessionConnected(context->sessionId))
		{
			if (WaitForSingleObject(context->stopEvent, 100) == WAIT_OBJECT_0)
				break;
			continue;
		}

		HANDLE waitHandles[] = { context->stopEvent, guiShutdown, guiRequest };
		const DWORD waitResult = WaitForMultipleObjects(3, waitHandles, FALSE, INFINITE);
		switch (waitResult)
		{
		case WAIT_OBJECT_0:
		case WAIT_OBJECT_0 + 1:
			running = false;
			break;
		case WAIT_OBJECT_0 + 2:
			lockGuiEffect();
			processBlock(guiHeader, *context->dspState);
			unlockGuiEffect();
			SetEvent(guiResponse);
			break;
		default:
			running = false;
			break;
		}
	}

	return 0;
}

static void resizeGuiWindowToEditor(int width, int height)
{
	if (guiWindow == NULL || guiChild == NULL || width <= 0 || height <= 0)
		return;

	SetWindowPos(guiChild, NULL, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);

	RECT rect = { 0, 0, width, height };
	AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
	SetWindowPos(guiWindow, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void showGuiWindow()
{
	if (guiWindow == NULL)
		return;

	ShowWindow(guiWindow, SW_SHOWNORMAL);
	BringWindowToTop(guiWindow);
	SetForegroundWindow(guiWindow);
	SetActiveWindow(guiWindow);
}

static void hideGuiWindow()
{
	if (guiWindow == NULL)
		return;

	ShowWindow(guiWindow, SW_HIDE);
	if (guiHiddenEvent != NULL)
		SetEvent(guiHiddenEvent);
}

static void writeGuiEffectState()
{
	if (guiEffect == nullptr || guiVstConfigPath.empty())
		return;

	OutProcVSTConfig config;
	if (!OutProcReadVSTConfig(guiVstConfigPath, config))
		return;

	lockGuiEffect();
	guiEffect->readFromEffect(config.chunkData, config.paramMap);
	unlockGuiEffect();

	OutProcWriteVSTConfig(guiVstConfigPath, config);
}

static LRESULT CALLBACK guiWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_SIZE:
		if (guiChild != NULL)
			SetWindowPos(guiChild, NULL, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		return 0;
	case WM_TIMER:
		if (guiShowEvent != NULL && WaitForSingleObject(guiShowEvent, 0) == WAIT_OBJECT_0)
			showGuiWindow();
		if (guiHideEvent != NULL && WaitForSingleObject(guiHideEvent, 0) == WAIT_OBJECT_0)
			hideGuiWindow();
		if (guiExitEvent != NULL && WaitForSingleObject(guiExitEvent, 0) == WAIT_OBJECT_0)
			DestroyWindow(hwnd);
		if (guiEffect != nullptr)
		{
			lockGuiEffect();
			guiEffect->doIdle();
			unlockGuiEffect();
		}
		{
			const ULONGLONG now = GetTickCount64();
			if (now - guiLastConfigWriteTick >= 500)
			{
				writeGuiEffectState();
				guiLastConfigWriteTick = now;
			}
		}
		if (guiMemoryLimitExceeded())
			ExitProcess(22);
		return 0;
	case WM_CLOSE:
		hideGuiWindow();
		return 0;
	case WM_DESTROY:
		KillTimer(hwnd, 1);
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
}

static void registerGuiWindowClasses()
{
	static bool registered = false;
	if (registered)
		return;

	WNDCLASSW wc = {};
	wc.lpfnWndProc = guiWindowProc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = guiWindowClassName;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	RegisterClassW(&wc);

	WNDCLASSW child = {};
	child.lpfnWndProc = DefWindowProcW;
	child.hInstance = GetModuleHandleW(NULL);
	child.lpszClassName = guiChildWindowClassName;
	child.hCursor = LoadCursor(NULL, IDC_ARROW);
	child.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	RegisterClassW(&child);

	registered = true;
}

static int runVstGuiHost(const std::wstring& vstConfigPath, const std::wstring& sessionId)
{
	appendDebugLog(L"start gui session=" + sessionId + L" config=" + vstConfigPath);
	OutProcVSTConfig config;
	if (vstConfigPath.empty() || !OutProcReadVSTConfig(vstConfigPath, config))
		return 10;

	HRESULT comResult = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
		return 11;

	InitializeCriticalSection(&guiEffectLock);
	guiEffectLockInitialized = true;

	int exitCode = 0;
	std::shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(config.libraryPath);
	std::unique_ptr<VSTPluginInstance> effect;
	HostDspState dspState;
	dspState.vstConfigPath = vstConfigPath;
	guiVstConfigPath = vstConfigPath;
	guiLastConfigWriteTick = 0;
	dspState.vst.config = config;
	dspState.vst.library = library;
	dspState.vst.usesGuiEffect = true;

	if (library == nullptr || library->initialize() < 0)
		exitCode = 12;
	else
	{
		effect.reset(new VSTPluginInstance(library, 1, config.vst3ClassIndex));
		if (!effect->initialize())
			exitCode = 13;
	}

	if (exitCode == 0)
	{
		writeSessionPidFile(sessionId);
		guiShowEvent = CreateEventW(NULL, FALSE, FALSE, makeSessionObjectName(sessionId, L"GuiShow").c_str());
		guiHideEvent = CreateEventW(NULL, FALSE, FALSE, makeSessionObjectName(sessionId, L"GuiHide").c_str());
		guiHiddenEvent = CreateEventW(NULL, FALSE, FALSE, makeSessionObjectName(sessionId, L"GuiHidden").c_str());
		guiExitEvent = CreateEventW(NULL, FALSE, FALSE, makeSessionObjectName(sessionId, L"GuiExit").c_str());
		guiInfoMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(OutProcGuiInfo), makeSessionObjectName(sessionId, L"GuiInfo").c_str());
		if (guiInfoMapping != NULL)
		{
			OutProcGuiInfo* info = static_cast<OutProcGuiInfo*>(MapViewOfFile(guiInfoMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(OutProcGuiInfo)));
			if (info != nullptr)
			{
				info->magic = OUTPROC_GUI_INFO_MAGIC;
				info->version = OUTPROC_GUI_INFO_VERSION;
				info->processId = GetCurrentProcessId();
				info->reserved = 0;
				UnmapViewOfFile(info);
			}
		}
		guiEffect = effect.get();
		effect->writeToEffect(config.chunkData, config.paramMap);
		effect->setSizeWindowFunc([](int width, int height) {
			resizeGuiWindowToEditor(width, height);
		});

		registerGuiWindowClasses();
		guiWindow = CreateWindowExW(0, guiWindowClassName, effect->getName().empty() ? L"VST Plugin" : effect->getName().c_str(),
			WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
			CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, GetModuleHandleW(NULL), NULL);
		if (guiWindow == NULL)
			exitCode = 14;
	}

	if (exitCode == 0)
	{
		guiChild = CreateWindowExW(0, guiChildWindowClassName, L"",
			WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
			0, 0, 640, 480, guiWindow, NULL, GetModuleHandleW(NULL), NULL);
		if (guiChild == NULL)
			exitCode = 15;
	}

	if (exitCode == 0)
	{
		short width = 640;
		short height = 480;
		if (!effect->startEditing(guiChild, &width, &height))
			exitCode = 16;
		else
		{
			resizeGuiWindowToEditor(width, height);
			showGuiWindow();
			UpdateWindow(guiWindow);
			SetTimer(guiWindow, 1, 1000 / 60, NULL);

			MSG msg;
			dspState.vst.effects.push_back(std::move(effect));
			GuiAudioThreadContext audioContext;
			audioContext.sessionId = sessionId;
			audioContext.dspState = &dspState;
			audioContext.stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
			HANDLE audioThread = audioContext.stopEvent != NULL
				? CreateThread(NULL, 0, guiAudioThreadProc, &audioContext, 0, NULL)
				: NULL;

			while (GetMessageW(&msg, NULL, 0, 0) > 0)
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}

			if (audioContext.stopEvent != NULL)
				SetEvent(audioContext.stopEvent);
			if (audioThread != NULL)
			{
				WaitForSingleObject(audioThread, 1000);
				CloseHandle(audioThread);
			}
			if (audioContext.stopEvent != NULL)
				CloseHandle(audioContext.stopEvent);

			if (!dspState.vst.effects.empty())
			{
				lockGuiEffect();
				dspState.vst.effects[0]->readFromEffect(config.chunkData, config.paramMap);
				unlockGuiEffect();
			}
			OutProcWriteVSTConfig(vstConfigPath, config);
		}
	}

	if (effect)
	{
		effect->setSizeWindowFunc(nullptr);
		effect->stopEditing();
		effect.reset();
	}
	if (!dspState.vst.effects.empty())
	{
		lockGuiEffect();
		dspState.vst.effects[0]->setSizeWindowFunc(nullptr);
		dspState.vst.effects[0]->stopEditing();
		unlockGuiEffect();
	}
	dspState.vst.effects.clear();
	guiEffect = nullptr;
	guiVstConfigPath.clear();
	guiChild = NULL;
	guiWindow = NULL;
	closeGuiSession();

	if (guiEffectLockInitialized)
	{
		DeleteCriticalSection(&guiEffectLock);
		guiEffectLockInitialized = false;
	}

	if (SUCCEEDED(comResult))
		CoUninitialize();

	deleteSessionPidFile(sessionId);
	return exitCode;
}

static bool validateHeader(OutProcAudioHeader* header)
{
	if (header == nullptr
		|| header->magic != OUTPROC_AUDIO_MAGIC
		|| header->version != OUTPROC_AUDIO_VERSION
		|| header->channelCount == 0
		|| header->maxFrames == 0
		|| header->frameCount == 0
		|| header->frameCount > header->maxFrames)
	{
		if (header != nullptr)
			header->status = OUTPROC_AUDIO_STATUS_ERROR;
		return false;
	}
	return true;
}

static void processGain(OutProcAudioHeader* header, HostDspState& state)
{
	const double targetGain = pow(10.0, header->gainDb / 20.0);
	if (!state.initialized)
	{
		state.currentGain = targetGain;
		state.initialized = true;
	}

	const std::uint32_t smoothingSamples = header->smoothingSamples == 0 ? 1 : header->smoothingSamples;
	const double gainStep = (targetGain - state.currentGain) / smoothingSamples;
	double* input = OutProcAudioInput(header);
	double* output = OutProcAudioOutput(header);

	double blockGain = state.currentGain;
	for (std::uint32_t frame = 0; frame < header->frameCount; ++frame)
	{
		if (blockGain != targetGain)
		{
			const double nextGain = blockGain + gainStep;
			if ((gainStep > 0.0 && nextGain > targetGain) || (gainStep < 0.0 && nextGain < targetGain))
				blockGain = targetGain;
			else
				blockGain = nextGain;
		}

		for (std::uint32_t channel = 0; channel < header->channelCount; ++channel)
		{
			const std::size_t offset = static_cast<std::size_t>(channel) * header->maxFrames;
			output[offset + frame] = input[offset + frame] * blockGain;
		}
	}

	state.currentGain = blockGain;
	header->hostState = state.initialized ? 1 : 0;
}

static void ensureBiquadState(OutProcAudioHeader* header, HostDspState& state)
{
	const bool coefficientsChanged = state.a0 != header->biquadA0
		|| state.a1 != header->biquadA1
		|| state.a2 != header->biquadA2
		|| state.b1 != header->biquadB1
		|| state.b2 != header->biquadB2;

	if (state.channelCount != header->channelCount || state.x1.size() != header->channelCount)
	{
		state.x1.assign(header->channelCount, 0.0);
		state.x2.assign(header->channelCount, 0.0);
		state.y1.assign(header->channelCount, 0.0);
		state.y2.assign(header->channelCount, 0.0);
		state.channelCount = header->channelCount;
	}

	if (coefficientsChanged)
	{
		state.a0 = header->biquadA0;
		state.a1 = header->biquadA1;
		state.a2 = header->biquadA2;
		state.b1 = header->biquadB1;
		state.b2 = header->biquadB2;
	}

	state.initialized = true;
}

static void processBiquad(OutProcAudioHeader* header, HostDspState& state)
{
	ensureBiquadState(header, state);

	double* input = OutProcAudioInput(header);
	double* output = OutProcAudioOutput(header);

	for (std::uint32_t channel = 0; channel < header->channelCount; ++channel)
	{
		const std::size_t offset = static_cast<std::size_t>(channel) * header->maxFrames;
		for (std::uint32_t frame = 0; frame < header->frameCount; ++frame)
		{
			const double sample = input[offset + frame];
			const double result = state.a0 * sample + state.a2 * state.x2[channel] + state.a1 * state.x1[channel]
				- state.b2 * state.y2[channel] - state.b1 * state.y1[channel];

			state.x2[channel] = state.x1[channel];
			state.x1[channel] = sample;
			state.y2[channel] = state.y1[channel];
			state.y1[channel] = result;
			output[offset + frame] = result;
		}
	}

	header->hostState = state.initialized ? 2 : 0;
}

static bool initializeVst(OutProcAudioHeader* header, VstRuntime& vst)
{
	if (vst.failed)
		return false;
	if (vst.initialized && vst.channelCount == header->channelCount && vst.maxFrames == header->maxFrames)
		return true;

	std::unique_ptr<VSTPluginInstance> preservedFirstEffect;
	if (!vst.effects.empty())
		preservedFirstEffect = std::move(vst.effects[0]);

	vst.effects.clear();
	vst.emptyChannels.clear();
	vst.inputArray.clear();
	vst.outputArray.clear();
	vst.floatInputStorage.clear();
	vst.floatOutputStorage.clear();
	vst.floatInputs.clear();
	vst.floatOutputs.clear();
	vst.initialized = false;

	if (vst.library == nullptr)
		vst.library = VSTPluginLibrary::getInstance(vst.config.libraryPath);
	if (vst.library == nullptr || vst.library->initialize() < 0)
	{
		vst.failed = true;
		return false;
	}

	std::unique_ptr<VSTPluginInstance> firstEffect;
	if (preservedFirstEffect)
	{
		firstEffect = std::move(preservedFirstEffect);
		firstEffect->setProcessLevel(2);
	}
	else
		firstEffect.reset(new VSTPluginInstance(vst.library, 2, vst.config.vst3ClassIndex));

	if (!firstEffect || (!firstEffect->numInputs() && !firstEffect->numOutputs() && !firstEffect->initialize()))
	{
		vst.failed = true;
		return false;
	}

	const int inputs = firstEffect->numInputs();
	const int outputs = firstEffect->numOutputs();
	vst.effectChannelCount = static_cast<std::uint32_t>(max(inputs, outputs));
	if (vst.effectChannelCount == 0)
	{
		vst.failed = true;
		return false;
	}

	const std::uint32_t effectCount = (header->channelCount + vst.effectChannelCount - 1) / vst.effectChannelCount;
	vst.effects.reserve(effectCount);
	vst.effects.push_back(std::move(firstEffect));
	for (std::uint32_t i = 1; i < effectCount; ++i)
	{
		std::unique_ptr<VSTPluginInstance> effect(new VSTPluginInstance(vst.library, 2, vst.config.vst3ClassIndex));
		if (!effect->initialize())
		{
			vst.failed = true;
			return false;
		}
		vst.effects.push_back(std::move(effect));
	}

	for (std::uint32_t i = 0; i < effectCount; ++i)
	{
		VSTPluginInstance* effect = vst.effects[i].get();
		if (i == effectCount - 1 && (header->channelCount % vst.effectChannelCount) != 0)
			effect->setUsedChannelCount(header->channelCount % vst.effectChannelCount);
		else
		effect->setUsedChannelCount(vst.effectChannelCount);
		effect->writeToEffect(vst.config.chunkData, vst.config.paramMap);
		effect->prepareForProcessing(static_cast<float>(header->sampleRate), static_cast<int>(header->maxFrames));
		effect->startProcessing();
	}

	const std::uint32_t emptyChannelCount = 2 * (effectCount * vst.effectChannelCount - header->channelCount);
	vst.emptyChannels.assign(emptyChannelCount, std::vector<double>(header->maxFrames, 0.0));
	vst.inputArray.resize(inputs);
	vst.outputArray.resize(outputs);
	vst.floatInputStorage.assign(inputs, std::vector<float>(header->maxFrames, 0.0f));
	vst.floatOutputStorage.assign(outputs, std::vector<float>(header->maxFrames, 0.0f));
	vst.floatInputs.resize(inputs);
	vst.floatOutputs.resize(outputs);
	for (int i = 0; i < inputs; ++i)
		vst.floatInputs[i] = vst.floatInputStorage[i].data();
	for (int i = 0; i < outputs; ++i)
		vst.floatOutputs[i] = vst.floatOutputStorage[i].data();

	vst.channelCount = header->channelCount;
	vst.maxFrames = header->maxFrames;
	vst.initialized = true;
	return true;
}

static bool processVst(OutProcAudioHeader* header, HostDspState& state)
{
	if (!initializeVst(header, state.vst))
		return false;

	double* input = OutProcAudioInput(header);
	double* output = OutProcAudioOutput(header);
	std::uint32_t channelOffset = 0;
	std::uint32_t emptyChannelIndex = 0;

	for (std::size_t effectIndex = 0; effectIndex < state.vst.effects.size(); ++effectIndex)
	{
		VSTPluginInstance* effect = state.vst.effects[effectIndex].get();

		for (int j = 0; j < effect->numInputs(); ++j)
		{
			if (channelOffset + j < header->channelCount)
				state.vst.inputArray[j] = input + static_cast<std::size_t>(channelOffset + j) * header->maxFrames;
			else
				state.vst.inputArray[j] = state.vst.emptyChannels[emptyChannelIndex++].data();
		}

		for (int j = 0; j < effect->numOutputs(); ++j)
		{
			if (channelOffset + j < header->channelCount)
				state.vst.outputArray[j] = output + static_cast<std::size_t>(channelOffset + j) * header->maxFrames;
			else
				state.vst.outputArray[j] = state.vst.emptyChannels[emptyChannelIndex++].data();
		}

		if (effect->canDoubleReplacing())
		{
			effect->processDoubleReplacing(state.vst.inputArray.data(), state.vst.outputArray.data(), static_cast<int>(header->frameCount));
		}
		else
		{
			for (int j = 0; j < effect->numInputs(); ++j)
				convertDoubleToFloat(state.vst.floatInputs[j], state.vst.inputArray[j], header->frameCount);

			if (effect->canReplacing())
				effect->processReplacing(state.vst.floatInputs.data(), state.vst.floatOutputs.data(), static_cast<int>(header->frameCount));
			else
			{
				for (int j = 0; j < effect->numOutputs(); ++j)
					memset(state.vst.floatOutputs[j], 0, header->frameCount * sizeof(float));
				effect->process(state.vst.floatInputs.data(), state.vst.floatOutputs.data(), static_cast<int>(header->frameCount));
			}

			for (int j = 0; j < effect->numOutputs(); ++j)
				convertFloatToDouble(state.vst.outputArray[j], state.vst.floatOutputs[j], header->frameCount);
		}

		if (effect->numOutputs() < effect->numInputs())
		{
			for (int j = effect->numOutputs(); j < effect->numInputs(); ++j)
			{
				if (channelOffset + j < header->channelCount)
					memset(output + static_cast<std::size_t>(channelOffset + j) * header->maxFrames, 0, header->frameCount * sizeof(double));
			}
		}

		channelOffset += state.vst.effectChannelCount;
	}

	header->hostState = 3;
	return true;
}

static bool processBlock(OutProcAudioHeader* header, HostDspState& state)
{
	if (!validateHeader(header))
		return false;

	if (header->dspType == OUTPROC_AUDIO_DSP_GAIN)
		processGain(header, state);
	else if (header->dspType == OUTPROC_AUDIO_DSP_BIQUAD)
		processBiquad(header, state);
	else if (header->dspType == OUTPROC_AUDIO_DSP_VST)
	{
		if (!processVst(header, state))
		{
			header->status = OUTPROC_AUDIO_STATUS_ERROR;
			return false;
		}
	}
	else
	{
		header->status = OUTPROC_AUDIO_STATUS_ERROR;
		return false;
	}

	header->status = OUTPROC_AUDIO_STATUS_OK;
	header->responseSeq = header->requestSeq;
	return true;
}

static int runHostMain(int argc, wchar_t** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

	std::wstring vstConfigPath = parseStringArg(argc, argv, L"--vst-config");
	std::wstring sessionId = parseStringArg(argc, argv, L"--session");
	if (hasFlag(argc, argv, L"--gui"))
		return runVstGuiHost(vstConfigPath, sessionId);

	HANDLE mapping = parseHandle(argc, argv, L"--mapping");
	HANDLE request = parseHandle(argc, argv, L"--request");
	HANDLE response = parseHandle(argc, argv, L"--response");
	HANDLE shutdown = parseHandle(argc, argv, L"--shutdown");

	if (mapping == NULL || request == NULL || response == NULL || shutdown == NULL)
		return 2;

	OutProcAudioHeader* header = static_cast<OutProcAudioHeader*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
	if (header == nullptr)
		return 3;

	HANDLE waitHandles[] = { shutdown, request };
	HostDspState dspState;
	dspState.vstConfigPath = vstConfigPath;
	if (!vstConfigPath.empty() && !OutProcReadVSTConfig(vstConfigPath, dspState.vst.config))
		return 4;

	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	bool running = true;
	while (running)
	{
		const DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
		switch (waitResult)
		{
		case WAIT_OBJECT_0:
			running = false;
			break;
		case WAIT_OBJECT_0 + 1:
			processBlock(header, dspState);
			SetEvent(response);
			break;
		default:
			running = false;
			break;
		}
	}

	UnmapViewOfFile(header);
	CoUninitialize();
	return 0;
}

int wmain(int argc, wchar_t** argv)
{
	return runHostMain(argc, argv);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == NULL)
		return 1;

	const int result = runHostMain(argc, argv);
	LocalFree(argv);
	return result;
}
