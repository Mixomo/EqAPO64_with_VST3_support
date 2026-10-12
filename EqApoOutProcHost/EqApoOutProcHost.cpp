#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <shellapi.h>
#include <avrt.h>
#pragma comment(lib,"avrt.lib")

#include <cmath>
#include <cstdint>
#include <cwchar>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>

#include "helpers/VSTPluginInstance.h"
#include "helpers/VSTPluginLibrary.h"
#include "helpers/VSTScan.h"
#include "outproc/OutProcAudioProtocol.h"
#include "outproc/OutProcGuiObjects.h"
#include "outproc/OutProcVSTConfig.h"
#include "outproc/OutProcControlEndpoint.h"
#include "NativeDspViewAudit.h"
#include "helpers/VSTGuiAudioMonitor.h"
#include "outproc/BrokerAudioSession.h"
#include "outproc/BrokerCatalog.h"
#include "outproc/BrokerInstanceChannel.h"
#include "outproc/HostStartupProgress.h"

struct HostApartment {
    HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ~HostApartment(){if(SUCCEEDED(result))CoUninitialize();}
};

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
    std::uint64_t warmupSamples=0;
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
static void processAsyncSlots(OutProcAudioHeader* header, HostDspState& state);

static const wchar_t* guiWindowClassName = L"EqApoOutProcVSTGuiWindow";
static const wchar_t* guiChildWindowClassName = L"EqApoOutProcVSTGuiChild";
static const SIZE_T guiPrivateBytesLimit = static_cast<SIZE_T>(1536) * 1024 * 1024;
static VSTPluginInstance* guiEffect = nullptr;
static std::unique_ptr<VSTGuiAudioMonitor> guiInputMonitor;
static std::wstring guiControlSession;
static std::uint64_t guiControlIdentity=0;
static ULONGLONG guiLastMonitorRetry=0;
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
	if (std::wstring(suffix).compare(0, 3, L"Gui") == 0)
        return OutProcGuiObjectName(sessionId, suffix);
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

	DWORD mmcssIndex=0;
    HANDLE mmcss=AvSetMmThreadCharacteristicsW(L"Pro Audio",&mmcssIndex);
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
		const DWORD waitResult = WaitForMultipleObjects(3, waitHandles, FALSE, 1);
		switch (waitResult)
		{
		case WAIT_OBJECT_0:
		case WAIT_OBJECT_0 + 1:
			running = false;
			break;
		case WAIT_TIMEOUT:
        case WAIT_OBJECT_0 + 2:
            if(TryEnterCriticalSection(&guiEffectLock)) {
                processAsyncSlots(guiHeader,*context->dspState);
                LeaveCriticalSection(&guiEffectLock);
            }
            break;
		default:
			running = false;
			break;
		}
	}

    if(mmcss) AvRevertMmThreadCharacteristics(mmcss);
	return 0;
}

static void resizeGuiWindowToEditor(int width, int height)
{
	if (guiWindow == NULL || guiChild == NULL || width <= 0 || height <= 0)
		return;

	SetWindowPos(guiChild, NULL, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);

	RECT rect = { 0, 0, width, height };
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow(guiWindow));
	SetWindowPos(guiWindow, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void showGuiWindow()
{
	if (guiWindow == NULL)
		return;

	OutProcShowGuiForeground(guiWindow);
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

	if(OutProcWriteVSTConfig(guiVstConfigPath,config) && guiInfoMapping) {
        auto* info=static_cast<OutProcGuiInfo*>(MapViewOfFile(guiInfoMapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(OutProcGuiInfo)));
        if(info) {info->stateGeneration.store(guiEffect->getStateGeneration(),std::memory_order_release);UnmapViewOfFile(info);}
    }
}

static LRESULT CALLBACK guiWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DPICHANGED:
        if(guiEffect) guiEffect->setEditorScale(float(HIWORD(wParam))/96.0f);
        if(lParam) {auto* rect=reinterpret_cast<RECT*>(lParam);SetWindowPos(hwnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);}
        return 0;
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
        {
            // Detach the plugin view while its parent still exists, below.
            PostQuitMessage(0);
            return 0;
        }
		if (guiEffect != nullptr)
		{
            if(guiInputMonitor && guiInputMonitor->waitingForInput() && GetTickCount64()-guiLastMonitorRetry>=200) {
                guiInputMonitor=std::make_unique<VSTGuiAudioMonitor>(guiEffect,guiControlSession,guiControlIdentity);
                guiLastMonitorRetry=GetTickCount64();
            }
			lockGuiEffect();
			guiEffect->doIdle();
            if(guiEffect->pendingRestartMask())guiEffect->serviceRestartRequests();
			unlockGuiEffect();
		}
		{
			const ULONGLONG now = GetTickCount64();
			static std::uint64_t savedGeneration=~std::uint64_t(0);
            static std::uint64_t observedGeneration=~std::uint64_t(0);
            static ULONGLONG lastEdit=0;
            const auto generation=guiEffect ? guiEffect->getStateGeneration() : 0;
            if(generation!=observedGeneration){observedGeneration=generation;lastEdit=now;}
            if (guiEffect && generation!=savedGeneration && now-lastEdit>=200)
			{
				writeGuiEffectState();
				guiLastConfigWriteTick = now;
                savedGeneration=generation;
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

	HostApartment apartment;
    const HRESULT comResult = apartment.result;
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
        if(!config.classUID.empty())config.vst3ClassIndex=library->findVST3ClassUID(config.classUID);
        if(config.vst3ClassIndex<0)exitCode=13;
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
				new(info) OutProcGuiInfo();
				info->magic = OUTPROC_GUI_INFO_MAGIC;
				info->version = OUTPROC_GUI_INFO_VERSION;
				info->processId = GetCurrentProcessId();
				info->reserved = 0;
				UnmapViewOfFile(info);
			}
			else exitCode = 19;
		}
		else exitCode = 19;
		guiEffect = effect.get();
        const auto controlIdentity=EqApoControl::controlIdentity(config.libraryPath,config.classUID,config.vst3ClassIndex);
        effect->setParameterSink([sessionId,controlIdentity](std::uint32_t id,double value,int offset) {
            EqApoControl::publishControl(sessionId,controlIdentity,id,value,offset);
        });
		effect->writeToEffect(config.chunkData, config.paramMap);
        effect->prepareForProcessing(48000,512);
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
            guiControlSession=sessionId;
            guiControlIdentity=EqApoControl::controlIdentity(config.libraryPath,config.classUID,config.vst3ClassIndex);
            guiInputMonitor=std::make_unique<VSTGuiAudioMonitor>(guiEffect,guiControlSession,guiControlIdentity);
            guiLastMonitorRetry=GetTickCount64();
			GuiAudioThreadContext audioContext;
			audioContext.sessionId = sessionId;
			audioContext.dspState = &dspState;
			audioContext.stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
			HANDLE audioThread = NULL; // GUI controller context never owns an APO processing session.

			while (GetMessageW(&msg, NULL, 0, 0) > 0)
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}

			if (audioContext.stopEvent != NULL)
				SetEvent(audioContext.stopEvent);
            guiInputMonitor.reset();
			if (audioThread != NULL)
			{
				if(WaitForSingleObject(audioThread,1000)!=WAIT_OBJECT_0) ExitProcess(23); // do not free a live worker's session
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
	if (guiWindow != NULL && IsWindow(guiWindow)) DestroyWindow(guiWindow);
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

static void startupAudit(const char* stage,long long duration=-1) {
    // Optional control-thread diagnostic. Never called for an audio block.
    wchar_t directory[32768]{};
    if(!GetEnvironmentVariableW(L"EQAPO_HOST_STARTUP_AUDIT",directory,32768))return;
    static const auto started=GetTickCount64();
    std::ofstream log(std::wstring(directory)+L"\\host-"+std::to_wstring(GetCurrentProcessId())+L".jsonl",std::ios::app);
    log<<"{\"stage\":\""<<stage<<"\",\"elapsed_ms\":"<<GetTickCount64()-started;
    if(duration>=0)log<<",\"duration_ms\":"<<duration;
    log<<"}\n";
}
static bool initializeVst(OutProcAudioHeader* header, VstRuntime& vst)
{
	if (vst.failed)
		return false;
	if (vst.initialized && vst.channelCount == header->channelCount && vst.maxFrames == header->maxFrames)
		return true;
    startupAudit("START_INITIALIZATION");

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

	if(!vst.config.classUID.empty()) {
        vst.config.vst3ClassIndex=vst.library->findVST3ClassUID(vst.config.classUID);
        if(vst.config.vst3ClassIndex<0) { vst.failed=true; return false; }
    }
    startupAudit("MODULE_INITIALIZED");
    startupAudit("LOAD_LIBRARY_DURATION",vst.library->moduleLoadMilliseconds());
    startupAudit("INIT_DLL_FACTORY_DURATION",vst.library->moduleInitializeMilliseconds());
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

    firstEffect->setUsedChannelCount(int(header->channelCount));
    startupAudit("COMPONENT_INITIALIZED");
    firstEffect->writeToEffect(vst.config.chunkData,vst.config.paramMap);
    startupAudit("FIRST_PRESET_RESTORED");
    firstEffect->prepareForProcessing(float(header->sampleRate),int(header->maxFrames));
    startupAudit("FIRST_SETUP_COMPLETED");
    if(!firstEffect->isProcessingValid()) {vst.failed=true;return false;}
	const int inputs = firstEffect->numInputs();
	const int outputs = firstEffect->numOutputs();
    if(inputs<0 || outputs<=0 || inputs>64 || outputs>64) {vst.failed=true;return false;}
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
		if(i!=0) {
			effect->writeToEffect(vst.config.chunkData, vst.config.paramMap);
            startupAudit("PRESET_RESTORED");
			effect->prepareForProcessing(static_cast<float>(header->sampleRate), static_cast<int>(header->maxFrames));
            startupAudit("PROCESSING_SETUP_COMPLETED");
		}
		effect->startProcessing();
        startupAudit("PROCESSOR_ACTIVATED");
	}

	const std::uint32_t emptyChannelCount = 2 * (effectCount * vst.effectChannelCount - header->channelCount);
	vst.emptyChannels.assign(emptyChannelCount, std::vector<double>(header->maxFrames, 0.0));
	vst.inputArray.resize(64);
	vst.outputArray.resize(64);
	vst.floatInputStorage.assign(64, std::vector<float>(header->maxFrames, 0.0f));
	vst.floatOutputStorage.assign(64, std::vector<float>(header->maxFrames, 0.0f));
	vst.floatInputs.resize(64);
	vst.floatOutputs.resize(64);
	for (int i = 0; i < 64; ++i)
		vst.floatInputs[i] = vst.floatInputStorage[i].data();
	for (int i = 0; i < 64; ++i)
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
    memset(output,0,std::size_t(header->channelCount)*header->maxFrames*sizeof(double));
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
            if(!effect->isProcessingValid()) return false;
		}
		else
		{
			for (int j = 0; j < effect->numInputs(); ++j)
				convertDoubleToFloat(state.vst.floatInputs[j], state.vst.inputArray[j], header->frameCount);

			if (effect->canReplacing())
            {
				effect->processReplacing(state.vst.floatInputs.data(), state.vst.floatOutputs.data(), static_cast<int>(header->frameCount));
                if(!effect->isProcessingValid()) return false;
            }
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

static std::uint64_t analysisThreadCpu100ns()
{
    FILETIME created{},exited{},kernel{},user{};
    if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return 0;
    return (std::uint64_t(kernel.dwHighDateTime)<<32)+kernel.dwLowDateTime+(std::uint64_t(user.dwHighDateTime)<<32)+user.dwLowDateTime;
}

static void processAsyncSlots(OutProcAudioHeader* h,HostDspState& state) {
    if(!h || h->magic!=OUTPROC_AUDIO_MAGIC || h->version!=OUTPROC_AUDIO_VERSION) return;
    h->heartbeat.fetch_add(1,std::memory_order_release);
    if(h->analysisOnly) {
        const auto seq=h->analysisRequest.load(std::memory_order_acquire);
        if(seq && seq!=h->analysisComplete.load(std::memory_order_relaxed)) {
            // CPU accounting is offline-only. A wait is not DSP CPU time;
            // realtime processing never calls GetThreadTimes.
            const auto before=analysisThreadCpu100ns();
            processBlock(h,state);
            const auto after=analysisThreadCpu100ns();
            if(after>=before)h->analysisCpu100ns.fetch_add(after-before,std::memory_order_release);
            h->analysisComplete.store(seq,std::memory_order_release);
        }
        return;
    }
    // Processing in ascending sequence preserves plugin state across variable callbacks.
    for(unsigned n=0;n<OUTPROC_AUDIO_SLOTS;++n) {
        OutProcAudioSlot* selected=nullptr;
        for(unsigned i=0;i<OUTPROC_AUDIO_SLOTS;++i) {
            auto* slot=OutProcSlot(h,i);
            if(slot->state.load(std::memory_order_acquire)==SLOT_INPUT && (!selected || slot->sequence<selected->sequence)) selected=slot;
        }
        if(!selected) break;
        EqApoControl::ParameterEvent parameter;
        for(unsigned count=0;count<EqApoControl::ParameterMailbox::capacity && h->parameters.consume(h->parameters.generation,parameter);++count) {
            bool accepted=!state.vst.effects.empty();
            for(auto& effect:state.vst.effects)accepted &= effect->applyParameterChangeOnDsp(parameter.id,parameter.normalized,parameter.sampleOffset);
            if(!accepted)h->parameterRejected.fetch_add(1,std::memory_order_relaxed);
            h->parameterSequence.store(parameter.sequence,std::memory_order_release);
        }
        std::uint32_t expected=SLOT_INPUT;
        if(!selected->state.compare_exchange_strong(expected,SLOT_PROCESSING,std::memory_order_acq_rel)) continue;
        if(selected->epoch!=h->sessionEpoch.load(std::memory_order_acquire)) {
            selected->status=OUTPROC_AUDIO_STATUS_ERROR;
            selected->state.store(SLOT_OUTPUT,std::memory_order_release);
            h->consumed.fetch_add(1,std::memory_order_relaxed);continue;
        }
        h->frameCount=selected->frames;
        h->requestSeq=selected->sequence;
        const auto samples=static_cast<std::size_t>(h->channelCount)*h->maxFrames;
        memcpy(OutProcAudioInput(h),OutProcSlotInput(selected),samples*sizeof(double));
        bool ok=processBlock(h,state);
        if(ok && state.warmupSamples)state.warmupSamples-= (std::min)(state.warmupSamples,std::uint64_t(selected->frames));
        if(ok && !state.vst.effects.empty()) {
            h->pluginLatency.store(state.vst.effects.front()->getInitialDelay(),std::memory_order_release);
            h->ready.store(state.warmupSamples ? 0u : 1u,std::memory_order_release);
        }
        selected->status=ok && !state.warmupSamples ? OUTPROC_AUDIO_STATUS_OK : OUTPROC_AUDIO_STATUS_ERROR;
        memcpy(OutProcSlotOutput(h,selected),OutProcAudioOutput(h),samples*sizeof(double));
        selected->state.store(SLOT_OUTPUT,std::memory_order_release);
        h->consumed.fetch_add(1,std::memory_order_relaxed);
    }
}

static int runHostMain(int argc, wchar_t** argv)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    if(hasFlag(argc,argv,L"--broker-stop") || hasFlag(argc,argv,L"--broker-status")){
        const auto user=EqApoControl::canonicalSid(EqApoControl::currentUserSid());if(user.empty())return 17;
        if(hasFlag(argc,argv,L"--broker-stop")){
            HANDLE stop=OpenEventW(EVENT_MODIFY_STATE,FALSE,(L"Local\\EqApoUserBroker_"+user+L"_Stop").c_str());
            if(!stop)return 21;const bool ok=SetEvent(stop)!=FALSE;CloseHandle(stop);return ok?0:21;
        }
        HANDLE guard=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(L"Local\\EqApoUserBroker_"+user).c_str());
        if(!guard)return 21;const auto result=WaitForSingleObject(guard,0);
        if(result==WAIT_OBJECT_0 || result==WAIT_ABANDONED)ReleaseMutex(guard);CloseHandle(guard);return result==WAIT_TIMEOUT?0:21;
    }
    if(hasFlag(argc,argv,L"--user-broker")){
        wchar_t executable[32768]{};if(!GetModuleFileNameW(nullptr,executable,32768))return 17;
        const auto user=EqApoControl::currentUserSid();const bool once=hasFlag(argc,argv,L"--broker-once");
        if(EqApoControl::canonicalSid(user).empty())return 17;
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        const auto acl=L"D:P(A;;GA;;;SY)(A;;GA;;;"+user+L")";
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return 17;
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
        HANDLE guard=CreateMutexW(&attributes,TRUE,(L"Local\\EqApoUserBroker_"+user).c_str());
        const bool duplicate=guard && GetLastError()==ERROR_ALREADY_EXISTS;
        if(!guard || duplicate){LocalFree(descriptor);if(guard)CloseHandle(guard);return duplicate?0:17;}
        struct Guard {HANDLE value;~Guard(){ReleaseMutex(value);CloseHandle(value);}} guardOwner{guard};
        HANDLE stop=CreateEventW(&attributes,TRUE,FALSE,(L"Local\\EqApoUserBroker_"+user+L"_Stop").c_str());LocalFree(descriptor);
        if(!stop)return 17;ResetEvent(stop);
        struct Stop {HANDLE value;~Stop(){CloseHandle(value);}} stopOwner{stop};
        const auto started=GetTickCount64();
        for(;;){
            for(const auto* space:{L"Global",L"Local"}){
                EqApoBroker::Catalog catalog;if(!catalog.open(space,user,false))continue;
                EqApoBroker::CatalogRequest request;
                while(catalog.take(request)){
                    if(request.controlHost[0]){
                        EqApoBroker::InstanceChannel instance;
                        if(instance.open(request.instanceHost,request.identity)){
                            HANDLE worker=OpenProcess(SYNCHRONIZE,FALSE,instance.getWorkerPid());
                            const bool alive=worker && WaitForSingleObject(worker,0)==WAIT_TIMEOUT;if(worker)CloseHandle(worker);
                            if(alive && instance.submit(request.id,request.space,request.generation,instance.getGeneration())){if(once)return 0;continue;}
                        }
                    }
                    std::wstring command=L"\""+std::wstring(executable)+L"\" --broker-session "+request.id+L" --broker-space "+request.space+L" --broker-generation "+std::to_wstring(request.generation);
                    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
                    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|NORMAL_PRIORITY_CLASS,nullptr,nullptr,&startup,&child))return 18;
                    CloseHandle(child.hThread);CloseHandle(child.hProcess);if(once)return 0;
                }
            }
            if(once && GetTickCount64()-started>5000)return 19;
            if(WaitForSingleObject(stop,250)==WAIT_OBJECT_0)return 0;
        }
    }

    if(hasFlag(argc,argv,L"--broker-launch")) {
        const auto id=parseStringArg(argc,argv,L"--broker-session"),space=parseStringArg(argc,argv,L"--broker-space");
        if(EqApoBroker::prefix(space,id,EqApoControl::currentUserSid()).empty())return 17;
        wchar_t executable[32768]{};if(!GetModuleFileNameW(nullptr,executable,32768))return 17;
        std::wstring command=L"\""+std::wstring(executable)+L"\" --broker-session "+id+L" --broker-space "+space;
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
        // No Editor parent watcher, inherited handles or GUI kill job. The
        // authenticated graph's shutdown event defines worker lifetime.
        if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|NORMAL_PRIORITY_CLASS,nullptr,nullptr,&startup,&child))return 18;
        CloseHandle(child.hThread);CloseHandle(child.hProcess);return 0;
    }
	const auto parentArgument=parseStringArg(argc,argv,L"--parent-pid");
    if(!parentArgument.empty()) {
        HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,wcstoul(parentArgument.c_str(),nullptr,10));
        if(!parent)return 24;
        HANDLE watcher=CreateThread(nullptr,0,[](LPVOID parameter)->DWORD {
            HANDLE parent=static_cast<HANDLE>(parameter);
            WaitForSingleObject(parent,INFINITE);CloseHandle(parent);ExitProcess(24);return 24;
        },parent,0,nullptr);
        if(!watcher) {CloseHandle(parent);return 24;}CloseHandle(watcher);
    }
    if(hasFlag(argc,argv,L"--scan"))return scanVSTPlugin(parseStringArg(argc,argv,L"--plugin"),parseStringArg(argc,argv,L"--result"));
	std::wstring vstConfigPath = parseStringArg(argc, argv, L"--vst-config");
	std::wstring sessionId = parseStringArg(argc, argv, L"--session");
	if (hasFlag(argc, argv, L"--gui"))
		return runVstGuiHost(vstConfigPath, sessionId);

	HANDLE mapping = parseHandle(argc, argv, L"--mapping");
	HANDLE request = parseHandle(argc, argv, L"--request");
	HANDLE response = parseHandle(argc, argv, L"--response");
	HANDLE shutdown = parseHandle(argc, argv, L"--shutdown");

    auto broker=std::make_unique<EqApoBroker::Client>();
    const auto brokerId=parseStringArg(argc,argv,L"--broker-session");
    if(!brokerId.empty()) {
        if(!parentArgument.empty() || !vstConfigPath.empty() || mapping || request || response || shutdown)return 17;
        if(!broker->open(parseStringArg(argc,argv,L"--broker-space"),brokerId))return 17;
        const auto generation=parseStringArg(argc,argv,L"--broker-generation");
        if(!generation.empty() && _wcstoui64(generation.c_str(),nullptr,10)!=broker->metadata->generation)return 17;
        mapping=broker->audio;request=broker->request;response=broker->response;shutdown=broker->shutdown;
    }
	if (mapping == NULL || request == NULL || response == NULL || shutdown == NULL)
		return 2;

	OutProcAudioHeader* header = static_cast<OutProcAudioHeader*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
	if (header == nullptr)
		return 3;

    HANDLE retire=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!retire){UnmapViewOfFile(header);return 18;}
    struct RetireEvent{HANDLE value;~RetireEvent(){CloseHandle(value);}} retireOwner{retire};
	HANDLE waitHandles[] = { shutdown, request, retire };
    const bool persistent=!brokerId.empty() && broker->metadata->controlGeneration!=0;
    EqApoBroker::InstanceChannel instance;
    if(persistent && !instance.create(broker->metadata->instanceSession,broker->metadata->identity)){UnmapViewOfFile(header);return 17;}
    struct IdleLease {
        std::atomic<ULONGLONG> deadline{0};HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::thread worker;
        void start(){worker=std::thread([this]{while(WaitForSingleObject(stop,250)==WAIT_TIMEOUT){const auto until=deadline.load();if(until && GetTickCount64()>until)TerminateProcess(GetCurrentProcess(),24);}});}
        ~IdleLease(){if(stop)SetEvent(stop);if(worker.joinable())worker.join();if(stop)CloseHandle(stop);}
    } idleLease;
    if(persistent){if(!idleLease.stop){UnmapViewOfFile(header);return 18;}idleLease.start();}
	// Declare the apartment before SDK ownership: SDK objects and module
    // teardown must finish before this thread leaves COM, including failures.
    HostApartment apartment;
    if(FAILED(apartment.result)){UnmapViewOfFile(header);return 6;}
    struct NativeMapping {
        EqApoControl::NativeViewChannel* channel=nullptr;
        HANDLE handle=nullptr;
        ~NativeMapping(){if(channel)UnmapViewOfFile(channel);if(handle)CloseHandle(handle);}
    } nativeMapping;
    const auto nativeHandle=parseHandle(argc,argv,L"--native-view");
    if(nativeHandle){nativeMapping.handle=nativeHandle;nativeMapping.channel=static_cast<EqApoControl::NativeViewChannel*>(MapViewOfFile(nativeHandle,FILE_MAP_ALL_ACCESS,0,0,sizeof(EqApoControl::NativeViewChannel)));}
    auto openNativeChannel=[&](const EqApoBroker::StateHeader& metadata,HANDLE& retainedHandle)->EqApoControl::NativeViewChannel* {
        const std::wstring controlSession=metadata.controlSession;
        for(const auto* space:{L"Global",L"Local"}){
            const auto prefix=std::wstring(space)+L"\\EqApoControl_"+controlSession+L"_"+EqApoControl::currentUserSid();
            HANDLE indexHandle=OpenFileMappingW(FILE_MAP_READ,FALSE,(prefix+L"_Current").c_str());if(!indexHandle)continue;
            const auto* index=static_cast<const EqApoControl::ControlIndex*>(MapViewOfFile(indexHandle,FILE_MAP_READ,0,0,sizeof(EqApoControl::ControlIndex)));
            const bool valid=index && index->magic.load(std::memory_order_acquire)==EqApoControl::controlMagic && index->version==EqApoControl::ControlIndex::protocolVersion
                && index->identity==metadata.identity && (index->generation.load(std::memory_order_acquire)==metadata.controlGeneration
                    || index->pendingGeneration.load(std::memory_order_acquire)==metadata.controlGeneration);
            if(index)UnmapViewOfFile(index);CloseHandle(indexHandle);if(!valid)continue;
            HANDLE view=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(prefix+L"_"+std::to_wstring(metadata.controlGeneration)+L"_View").c_str());
            if(!view)continue;
            auto* channel=static_cast<EqApoControl::NativeViewChannel*>(MapViewOfFile(view,FILE_MAP_ALL_ACCESS,0,0,sizeof(EqApoControl::NativeViewChannel)));
            if(channel && channel->magic==EqApoControl::NativeViewChannel::magicValue && channel->version==EqApoControl::NativeViewChannel::protocolVersion
                && channel->generation==metadata.controlGeneration && channel->identity==metadata.identity){retainedHandle=view;return channel;}
            if(channel)UnmapViewOfFile(channel);CloseHandle(view);
        }
        return nullptr;
    };
    if(!brokerId.empty() && broker->metadata->controlGeneration){
        sessionId=broker->metadata->controlSession;nativeMapping.channel=openNativeChannel(*broker->metadata,nativeMapping.handle);
        if(!nativeMapping.channel){UnmapViewOfFile(header);return 7;}
    }
    if(nativeMapping.channel && (nativeMapping.channel->magic!=EqApoControl::NativeViewChannel::magicValue || nativeMapping.channel->version!=EqApoControl::NativeViewChannel::protocolVersion))return 7;
    // Queue the first native-panel request while a cold vendor DLL is loading,
    // rather than start an unrelated GUI copy and load that DLL a second time.
    if(nativeMapping.channel){
        DWORD session=0;nativeMapping.channel->hostPid.store(GetCurrentProcessId(),std::memory_order_release);
        if(!ProcessIdToSessionId(GetCurrentProcessId(),&session) || !session)nativeMapping.channel->status.store(4,std::memory_order_release);
    }
    HostDspState dspState;
	dspState.vstConfigPath = vstConfigPath;
	if(!brokerId.empty())dspState.vst.config=broker->config;
	if (!vstConfigPath.empty() && !OutProcReadVSTConfig(vstConfigPath, dspState.vst.config))
		return 4;

    if(header->dspType==OUTPROC_AUDIO_DSP_VST) {
        HostStartupProgress startupProgress(header->heartbeat);
        if(!startupProgress.valid()) {UnmapViewOfFile(header);return 28;}
        const bool initialized=initializeVst(header,dspState.vst);
        startupProgress.stop();
        if(!initialized) {header->lastError.store(2,std::memory_order_release);UnmapViewOfFile(header);return 5;}
        header->pluginLatency.store(unsigned(dspState.vst.effects.front()->getInitialDelay()),std::memory_order_release);
        if(!header->analysisOnly)dspState.warmupSamples=std::uint64_t((std::max)(0,dspState.vst.effects.front()->getInitialDelay()))+2*header->maxFrames;
        header->ready.store(dspState.warmupSamples ? 0u : 1u,std::memory_order_release);
    } else {
        header->frameCount=0;
        if(header->dspType==OUTPROC_AUDIO_DSP_GAIN)processGain(header,dspState);
        else if(header->dspType==OUTPROC_AUDIO_DSP_BIQUAD)processBiquad(header,dspState);
        header->ready.store(1,std::memory_order_release);
    }
    std::atomic<unsigned> owner{0};std::atomic<bool> finished{false};
    auto processPanelSilence=[&]() {
        EqApoControl::ParameterEvent parameter;
        for(unsigned count=0;count<EqApoControl::ParameterMailbox::capacity
            && header->parameters.consume(header->parameters.generation,parameter);++count) {
            for(auto& effect:dspState.vst.effects)effect->applyParameterChangeOnDsp(parameter.id,parameter.normalized,parameter.sampleOffset);
        }
        memset(OutProcAudioInput(header),0,std::size_t(header->channelCount)*header->maxFrames*sizeof(double));
        for(auto& buffer:dspState.vst.emptyChannels)std::fill(buffer.begin(),buffer.end(),0.);
        header->frameCount=header->maxFrames;
        return processVst(header,dspState);
    };
    auto runProcessor=[&]() {
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        DWORD mmcssIndex=0;HANDLE mmcss=AvSetMmThreadCharacteristicsW(L"Pro Audio",&mmcssIndex);
        const DWORD panelInterval=(std::max)(1u,static_cast<unsigned>(std::ceil(1000.0*header->maxFrames/header->sampleRate)));
        ULONGLONG lastAudioRequest=GetTickCount64();
        bool running=true;
        while(running) {
            // FilterEngine keeps its previous graph until the next reload.
            // A bypassed graph can therefore stop receiving audio before its
            // shutdown event is signalled. Keep its panel ticking in that gap.
            const DWORD wake=WaitForMultipleObjects(3,waitHandles,FALSE,persistent?panelInterval:INFINITE);
            if(wake==WAIT_OBJECT_0 || wake==WAIT_OBJECT_0+2 || wake==WAIT_FAILED)break;
            const bool silentPanelTick=wake==WAIT_TIMEOUT;
            if(!silentPanelTick)lastAudioRequest=GetTickCount64();
            if(silentPanelTick && GetTickCount64()-lastAudioRequest<100)continue;
            if(silentPanelTick && (!nativeMapping.channel
                || (nativeMapping.channel->status.load()!=2 && nativeMapping.channel->status.load()!=3)))continue;
            unsigned expected=0;
            if(!owner.compare_exchange_strong(expected,1,std::memory_order_acquire)) {
                header->heartbeat.fetch_add(1,std::memory_order_release);continue;
            }
            const auto analysisBefore=header->analysisComplete.load(std::memory_order_acquire);
            if(silentPanelTick) {
                if(!processPanelSilence())header->lastError.store(3);
            } else if(header->dspType!=OUTPROC_AUDIO_DSP_VST && !header->asyncTransport) {
                if(header->requestSeq!=header->responseSeq) {processBlock(header,dspState);SetEvent(response);}
            } else processAsyncSlots(header,dspState);
            if(header->analysisOnly && header->analysisComplete.load(std::memory_order_acquire)!=analysisBefore)SetEvent(response);
            owner.store(0,std::memory_order_release);
        }
        if(mmcss)AvRevertMmThreadCharacteristics(mmcss);
        CoUninitialize();finished.store(true,std::memory_order_release);
    };
    std::thread processor(runProcessor);
    EqApoControl::ControlEndpointOwner rebindAuditEndpoint;
    NativeMapping rebindAuditMapping;
    wchar_t rebindOption[4]{};const bool rebindAudit=GetEnvironmentVariableW(L"EQAPO_NATIVE_VIEW_REBIND_AUDIT",rebindOption,4)==1 && rebindOption[0]==L'1';
    if(rebindAudit && (dspState.vst.config.libraryPath.substr(dspState.vst.config.libraryPath.find_last_of(L"\\/")+1)!=L"EqApoFixture.vst3"
        || nativeMapping.channel || header->analysisOnly))TerminateProcess(GetCurrentProcess(),25);
    const auto rebindStarted=GetTickCount64();bool rebindDone=false,rebindClosed=false;unsigned rebindClicks=0,rebindRelayed=0;
    NativeDspViewAudit nativeViewAudit(nativeMapping.channel,sessionId,dspState.vst.config);
    nativeViewAudit.setRetainedParameterSink([&](std::uint32_t id,double value,int offset){
        return header->parameters.publish(header->parameters.generation,id,value,offset);
    });
    // Once audible transport is retired, an owned panel may still need DSP
    // ticks for meter decay and controller animation. This private silent
    // preview never publishes slots or contributes to the audible dry path.
    struct RetainedPanelProcessor {
        HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        std::atomic<bool> enabled{false};std::thread worker;
        void end(){enabled.store(false);if(stop)SetEvent(stop);if(worker.joinable())worker.join();}
        ~RetainedPanelProcessor(){end();if(stop)CloseHandle(stop);}
    } panelPreview;
    auto startPanelPreview=[&]() {
        if(panelPreview.worker.joinable() || !panelPreview.stop)return;
        ResetEvent(panelPreview.stop);
        panelPreview.worker=std::thread([&]() {
            CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            const DWORD interval=(std::max)(1u,static_cast<unsigned>(std::ceil(1000.0*header->maxFrames/header->sampleRate)));
            while(WaitForSingleObject(panelPreview.stop,interval)==WAIT_TIMEOUT) {
                if(!panelPreview.enabled.load(std::memory_order_acquire))continue;
                unsigned expected=0;
                if(!owner.compare_exchange_strong(expected,1,std::memory_order_acquire))continue;
                if(!processPanelSilence()){header->lastError.store(3);panelPreview.enabled.store(false);}
                owner.store(0,std::memory_order_release);
            }
            CoUninitialize();
        });
    };
    std::unique_ptr<EqApoBroker::Client> nextBroker;
    OutProcAudioHeader* nextHeader=nullptr;EqApoControl::NativeViewChannel* nextView=nullptr;HANDLE nextViewHandle=nullptr;
    std::uint64_t nextSequence=0,appliedSequence=0;
    bool idle=false;
    while(true) {
        if(persistent){
            EqApoBroker::InstanceRequest handoff;
            if(!nextBroker && instance.take(handoff)){
                auto candidate=std::make_unique<EqApoBroker::Client>();
                bool valid=handoff.sequence>appliedSequence && candidate->open(handoff.space,handoff.session);
                if(valid)valid=candidate->metadata->generation==handoff.transportGeneration && candidate->metadata->identity==broker->metadata->identity
                    && std::wstring(candidate->metadata->controlSession)==sessionId
                    && std::wstring(candidate->metadata->instanceSession)==broker->metadata->instanceSession
                    && candidate->metadata->channels==header->channelCount
                    && candidate->config.libraryPath==dspState.vst.config.libraryPath
                    && candidate->config.vst3ClassIndex==dspState.vst.config.vst3ClassIndex
                    && candidate->config.classUID==dspState.vst.config.classUID;
                if(valid){
                    nextHeader=static_cast<OutProcAudioHeader*>(MapViewOfFile(candidate->audio,FILE_MAP_ALL_ACCESS,0,0,0));
                    nextView=openNativeChannel(*candidate->metadata,nextViewHandle);valid=nextHeader && nextView;
                }
                if(valid){nextSequence=handoff.sequence;nextBroker=std::move(candidate);idleLease.deadline.store(GetTickCount64()+5000);SetEvent(retire);}
                else {if(nextHeader)UnmapViewOfFile(nextHeader);if(nextView)UnmapViewOfFile(nextView);if(nextViewHandle)CloseHandle(nextViewHandle);nextHeader=nullptr;nextView=nullptr;nextViewHandle=nullptr;instance.acknowledge(handoff.sequence,false);}
            }
        }
        if(finished.load(std::memory_order_acquire)){
            if(!persistent)break;
            if(!idle){if(processor.joinable())processor.join();broker->stopLeaseWatch();header->ready.store(0);idle=true;idleLease.deadline.store(GetTickCount64()+5000);}
            // An open native editor owns the retired instance during bypass.
            // Keep visible and hidden views alive; release normally five seconds
            // after the window closes. Handoff itself retains its bounded deadline.
            const auto retainedView=nativeViewAudit.getWindowForAudit();
            if(!nextBroker && retainedView && IsWindow(retainedView))
                idleLease.deadline.store(GetTickCount64()+5000);
            if(!nextBroker && GetTickCount64()+500>=idleLease.deadline.load())break;
            if(nextBroker){
                panelPreview.end();
                for(auto& effect:dspState.vst.effects)effect->stopProcessing();
                if(dspState.vst.maxFrames!=nextHeader->maxFrames){
                    for(auto& empty:dspState.vst.emptyChannels)empty.assign(nextHeader->maxFrames,0.);
                    dspState.vst.floatInputStorage.assign(64,std::vector<float>(nextHeader->maxFrames,0.f));
                    dspState.vst.floatOutputStorage.assign(64,std::vector<float>(nextHeader->maxFrames,0.f));
                    for(unsigned i=0;i<64;++i){dspState.vst.floatInputs[i]=dspState.vst.floatInputStorage[i].data();dspState.vst.floatOutputs[i]=dspState.vst.floatOutputStorage[i].data();}
                    dspState.vst.maxFrames=nextHeader->maxFrames;
                }
                for(auto& effect:dspState.vst.effects){effect->writeToEffect(nextBroker->config.chunkData,nextBroker->config.paramMap);effect->prepareForProcessing(float(nextHeader->sampleRate),int(nextHeader->maxFrames));if(!effect->isProcessingValid())TerminateProcess(GetCurrentProcess(),26);effect->startProcessing();}
                if(!nativeViewAudit.rebind(nextView,sessionId,nextBroker->metadata->identity))TerminateProcess(GetCurrentProcess(),25);
                if(nativeMapping.channel)UnmapViewOfFile(nativeMapping.channel);if(nativeMapping.handle)CloseHandle(nativeMapping.handle);
                nativeMapping.channel=nextView;nativeMapping.handle=nextViewHandle;nextView=nullptr;nextViewHandle=nullptr;
                UnmapViewOfFile(header);header=nextHeader;nextHeader=nullptr;broker=std::move(nextBroker);
                mapping=broker->audio;request=broker->request;response=broker->response;shutdown=broker->shutdown;
                waitHandles[0]=shutdown;waitHandles[1]=request;ResetEvent(retire);dspState.vst.config=broker->config;
                header->pluginLatency.store(unsigned(dspState.vst.effects.front()->getInitialDelay()));dspState.warmupSamples=header->pluginLatency.load()+2*header->maxFrames;
                header->ready.store(0);finished.store(false);owner.store(0);processor=std::thread(runProcessor);
                idle=false;idleLease.deadline.store(0);appliedSequence=nextSequence;instance.acknowledge(appliedSequence,true);
            }
        }
        // Controller, structural changes and the Windows message loop stay on
        // the main thread. The DSP worker never waits for this owner.
        MsgWaitForMultipleObjects(idle?0:1,idle?nullptr:&shutdown,FALSE,10,QS_ALLINPUT);
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        for(auto& effect:dspState.vst.effects)effect->serviceControllerParameterChanges();
        if(!header->analysisOnly && !dspState.vst.effects.empty())nativeViewAudit.service(dspState.vst.effects.front().get());
        const auto previewWindow=nativeViewAudit.getWindowForAudit();
        const bool previewEnabled=idle && previewWindow && IsWindow(previewWindow);
        panelPreview.enabled.store(previewEnabled,std::memory_order_release);
        if(previewEnabled)startPanelPreview();
        if(rebindAudit && !rebindDone && GetTickCount64()-rebindStarted>=2500){
            const auto auditSession=L"RebindAudit_"+std::to_wstring(GetCurrentProcessId());
            const auto identity=EqApoControl::controlIdentity(dspState.vst.config.libraryPath,dspState.vst.config.classUID,dspState.vst.config.vst3ClassIndex);
            if(!rebindAuditEndpoint.create(auditSession,EqApoControl::currentUserSid(),identity))TerminateProcess(GetCurrentProcess(),25);
            rebindAuditMapping.channel=static_cast<EqApoControl::NativeViewChannel*>(MapViewOfFile(rebindAuditEndpoint.getNativeViewHandle(),FILE_MAP_ALL_ACCESS,0,0,sizeof(EqApoControl::NativeViewChannel)));
            const auto retainedWindow=nativeViewAudit.getWindowForAudit();
            if(!retainedWindow || !IsWindow(retainedWindow) || nativeViewAudit.rebind(rebindAuditMapping.channel,auditSession,identity^1)
                || !nativeViewAudit.rebind(rebindAuditMapping.channel,auditSession,identity)
                || nativeViewAudit.getWindowForAudit()!=retainedWindow || !IsWindow(retainedWindow))TerminateProcess(GetCurrentProcess(),25);
            rebindDone=true;
        }
        if(rebindAudit && rebindDone && !rebindClosed && GetTickCount64()-rebindStarted>=5000){rebindAuditMapping.channel->command.store(3);rebindClosed=true;}
        if(rebindAudit && rebindDone && rebindClicks<2 && GetTickCount64()-rebindStarted>=3200+rebindClicks*200){
            HWND fixture=nullptr;
            EnumChildWindows(nativeViewAudit.getWindowForAudit(),[](HWND child,LPARAM result)->BOOL{
                wchar_t name[128]{};GetClassNameW(child,name,128);
                if(std::wstring(name)==L"EqApoFixtureMeter"){*reinterpret_cast<HWND*>(result)=child;return FALSE;}return TRUE;
            },reinterpret_cast<LPARAM>(&fixture));
            if(!fixture || !PostMessageW(fixture,WM_LBUTTONUP,0,MAKELPARAM(100,100)))TerminateProcess(GetCurrentProcess(),25);
            ++rebindClicks;
        }
        if(rebindAudit && rebindDone){
            EqApoControl::ParameterEvent edit;
            while(rebindAuditEndpoint.consume(edit)){
                nativeViewAudit.recordRelayForAudit(edit.id,edit.normalized,rebindRelayed);
                if(edit.id!=7 || rebindRelayed>=2 || std::abs(edit.normalized-(rebindRelayed==0?1.:.25))>.00001
                    || !header->parameters.publish(header->parameters.generation,edit.id,edit.normalized,edit.sampleOffset))TerminateProcess(GetCurrentProcess(),25);
                ++rebindRelayed;SetEvent(request);
            }
            if(GetTickCount64()-rebindStarted>=6000 && rebindRelayed!=2)TerminateProcess(GetCurrentProcess(),25);
        }
        bool pending=false;
        for(const auto& effect:dspState.vst.effects)pending|=effect->hasControlRequests();
        if(!pending)continue;
        unsigned expected=0;
        if(!owner.compare_exchange_strong(expected,2,std::memory_order_acquire))continue;
        std::uint32_t structuralFlags=0;
        for(const auto& effect:dspState.vst.effects)structuralFlags|=effect->pendingRestartMask();
        const bool structural=structuralFlags & (Steinberg::Vst::kReloadComponent|Steinberg::Vst::kIoChanged|Steinberg::Vst::kLatencyChanged);
        if(structural)nativeViewAudit.close();
        if(structural && !header->analysisOnly) {
            header->ready.store(0,std::memory_order_release);
            header->sessionEpoch.fetch_add(1,std::memory_order_acq_rel);
        }
        for(auto& effect:dspState.vst.effects) {
            if(!effect->serviceRestartRequests()) {header->lastError.store(3,std::memory_order_release);ExitProcess(23);}
        }
        if(!dspState.vst.effects.empty()) {
            auto* first=dspState.vst.effects.front().get();
            const auto channelShape=unsigned((std::max)(first->numInputs(),first->numOutputs()));
            if(channelShape!=dspState.vst.effectChannelCount) {
                first->readFromEffect(dspState.vst.config.chunkData,dspState.vst.config.paramMap);
                dspState.vst.initialized=false;
                if(!initializeVst(header,dspState.vst))ExitProcess(23);
            }
            header->pluginLatency.store(unsigned(dspState.vst.effects.front()->getInitialDelay()),std::memory_order_release);
            if(structural && !header->analysisOnly)dspState.warmupSamples=std::uint64_t((std::max)(0,dspState.vst.effects.front()->getInitialDelay()))+2*header->maxFrames;
        }
        owner.store(0,std::memory_order_release);
        // A request consumed while control owned the component remains in
        // its input slot. Wake the dedicated worker after releasing ownership.
        SetEvent(request);
    }
    panelPreview.end();
    nativeViewAudit.close();
    if(processor.joinable())processor.join();

	UnmapViewOfFile(header);
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
