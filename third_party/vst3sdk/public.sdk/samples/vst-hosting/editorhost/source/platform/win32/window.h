//-----------------------------------------------------------------------------
// Flags       : clang-format auto
// Project     : VST SDK
//
// Category    : EditorHost
// Filename    : public.sdk/samples/vst-hosting/editorhost/source/platform/win32/window.h
// Created by  : Steinberg 09.2016
// Description : Example of opening a plug-in editor
//
//-----------------------------------------------------------------------------
// This file is part of a Steinberg SDK. It is subject to the license terms
// in the LICENSE file found in the top-level directory of this distribution
// and at www.steinberg.net/sdklicenses.
// No part of the SDK, including this file, may be copied, modified, propagated,
// or distributed except according to the terms contained in the LICENSE file.
//-----------------------------------------------------------------------------

#pragma once

#include "public.sdk/samples/vst-hosting/editorhost/source/platform/iwindow.h"
#include "public.sdk/source/vst/utility/optional.h"
#include <windows.h>
#include <vector>

//------------------------------------------------------------------------
namespace Steinberg {
namespace Vst {
namespace EditorHost {

//------------------------------------------------------------------------
class Window : public IWindow, public std::enable_shared_from_this<Window>
{
public:
	static WindowPtr make (const std::string& name, Size size, bool resizeable,
	                       const WindowControllerPtr& controller, HINSTANCE instance);

	bool init (const std::string& name, Size size, bool resizeable,
	           const WindowControllerPtr& controller, HINSTANCE instance);

	void show () override;
	void close () override;
	void resize (Size newSize) override;
	Size getContentSize () override;

	NativePlatformWindow getNativePlatformWindow () const override;

	tresult queryInterface (const TUID /*iid*/, void** /*obj*/) override { return kNoInterface; }

	void closeImmediately ();

	using WindowList = std::vector<Window*>;
	static WindowList getWindows ();

private:
	LRESULT CALLBACK proc (UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK WndProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	float getContentScaleFactor () const;
	void registerWindowClass (HINSTANCE instance);

	WindowPtr This;
	WindowControllerPtr controller {nullptr};
	HWND hwnd {nullptr};

	bool inDpiChangeState {false};
	Size dpiChangedSize {};
};

//------------------------------------------------------------------------
struct DynamicLibrary
{
	DynamicLibrary (const char* name) { module = LoadLibraryA (name); }

	~DynamicLibrary () { FreeLibrary (module); }

	template <typename T>
	T getProcAddress (const char* name)
	{
		return module ? reinterpret_cast<T> (GetProcAddress (module, name)) : nullptr;
	}

private:
	HMODULE module {nullptr};
};

#ifndef _DPI_AWARENESS_CONTEXTS_

DECLARE_HANDLE (DPI_AWARENESS_CONTEXT);

typedef enum DPI_AWARENESS {
	DPI_AWARENESS_INVALID = -1,
	DPI_AWARENESS_UNAWARE = 0,
	DPI_AWARENESS_SYSTEM_AWARE = 1,
	DPI_AWARENESS_PER_MONITOR_AWARE = 2
} DPI_AWARENESS;

#define DPI_AWARENESS_CONTEXT_UNAWARE ((DPI_AWARENESS_CONTEXT)-1)
#define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE ((DPI_AWARENESS_CONTEXT)-2)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((DPI_AWARENESS_CONTEXT)-3)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)

#endif

struct User32Library : DynamicLibrary
{
	static User32Library& instance ()
	{
		static User32Library gInstance;
		return gInstance;
	}

	bool setProcessDpiAwarenessContext (DPI_AWARENESS_CONTEXT context) const
	{
		if (!setProcessDpiAwarenessContextProc)
			return false;
		return setProcessDpiAwarenessContextProc (context);
	}

	bool adjustWindowRectExForDpi (LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle,
	                               UINT dpi) const
	{
		if (!adjustWindowRectExForDpiProc)
			return false;
		return adjustWindowRectExForDpiProc (lpRect, dwStyle, bMenu, dwExStyle, dpi);
	}

private:
	using SetProcessDpiAwarenessContextProc = BOOL (WINAPI*) (DPI_AWARENESS_CONTEXT);
	using AdjustWindowRectExForDpiProc = BOOL (WINAPI*) (LPRECT, DWORD, BOOL, DWORD, UINT);

	User32Library () : DynamicLibrary ("User32.dll")
	{
		setProcessDpiAwarenessContextProc =
		    getProcAddress<SetProcessDpiAwarenessContextProc> ("SetProcessDpiAwarenessContext");
		adjustWindowRectExForDpiProc =
		    getProcAddress<AdjustWindowRectExForDpiProc> ("AdjustWindowRectExForDpi");
	}

	SetProcessDpiAwarenessContextProc setProcessDpiAwarenessContextProc {nullptr};
	AdjustWindowRectExForDpiProc adjustWindowRectExForDpiProc {nullptr};
};

struct ShcoreLibrary : DynamicLibrary
{
	static ShcoreLibrary& instance ()
	{
		static ShcoreLibrary gInstance;
		return gInstance;
	}

	struct DPI
	{
		UINT x;
		UINT y;
	};

	VST3::Optional<DPI> getDpiForWindow (HWND window) const
	{
		if (!getDpiForMonitorProc)
			return {};
		auto monitor = MonitorFromWindow (window, MONITOR_DEFAULTTONEAREST);
		UINT x {}, y {};
		getDpiForMonitorProc (monitor, MDT_EFFECTIVE_DPI, &x, &y);
		return DPI {x, y};
	}

	HRESULT setProcessDpiAwareness (bool perMonitor = true)
	{
		if (perMonitor)
		{
			if (User32Library::instance ().setProcessDpiAwarenessContext (
			        perMonitor ? DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 :
			                     DPI_AWARENESS_CONTEXT_UNAWARE))
				return true;
		}
		if (!setProcessDpiAwarenessProc)
			return S_FALSE;
		return setProcessDpiAwarenessProc (perMonitor ? PROCESS_PER_MONITOR_DPI_AWARE :
		                                                PROCESS_SYSTEM_DPI_AWARE);
	}

private:
	enum PROCESS_DPI_AWARENESS
	{
		PROCESS_DPI_UNAWARE = 0,
		PROCESS_SYSTEM_DPI_AWARE = 1,
		PROCESS_PER_MONITOR_DPI_AWARE = 2
	};

	enum MONITOR_DPI_TYPE
	{
		MDT_EFFECTIVE_DPI = 0,
		MDT_ANGULAR_DPI = 1,
		MDT_RAW_DPI = 2,
		MDT_DEFAULT = MDT_EFFECTIVE_DPI
	};

	using GetDpiForMonitorProc = HRESULT (WINAPI*) (HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);
	using SetProcessDpiAwarenessProc = HRESULT (WINAPI*) (PROCESS_DPI_AWARENESS);

	ShcoreLibrary () : DynamicLibrary ("Shcore.dll")
	{
		getDpiForMonitorProc = getProcAddress<GetDpiForMonitorProc> ("GetDpiForMonitor");
		setProcessDpiAwarenessProc =
		    getProcAddress<SetProcessDpiAwarenessProc> ("SetProcessDpiAwareness");
	}

	GetDpiForMonitorProc getDpiForMonitorProc {nullptr};
	SetProcessDpiAwarenessProc setProcessDpiAwarenessProc {nullptr};
};

#ifndef DPI_ENUMS_DECLARED

#define DPI_ENUMS_DECLARED
#endif // (DPI_ENUMS_DECLARED)

//------------------------------------------------------------------------
} // EditorHost
} // Vst
} // Steinberg
