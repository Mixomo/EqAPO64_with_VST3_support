//-----------------------------------------------------------------------------
// Flags       : clang-format auto
// Project     : VST SDK
//
// Category    : EditorHost
// Filename    : public.sdk/samples/vst-hosting/editorhost/source/platform/win32/platform.cpp
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

#include "public.sdk/samples/vst-hosting/editorhost/source/platform/iplatform.h"
#include "public.sdk/samples/vst-hosting/editorhost/source/platform/win32/window.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <shellapi.h>
#include <string>
#include <vector>

//------------------------------------------------------------------------
namespace Steinberg {
namespace Vst {
namespace EditorHost {

//------------------------------------------------------------------------
class Platform : public IPlatform
{
public:
	static Platform& instance ()
	{
		static Platform gInstance;
		return gInstance;
	}

	void setApplication (ApplicationPtr&& app) override;
	WindowPtr createWindow (const std::string& title, Size size, bool resizeable,
	                        const WindowControllerPtr& controller) override;
	void quit () override;
	void kill (int resultCode, const std::string& reason) override;

	FUnknown* getPluginFactoryContext () override;

	void run (LPWSTR lpCmdLine, HINSTANCE instance);

private:
	ApplicationPtr application;
	HINSTANCE hInstance {nullptr};
	bool quitRequested {false};
};

//------------------------------------------------------------------------
IPlatform& IPlatform::instance ()
{
	return Platform::instance ();
}

//------------------------------------------------------------------------
void Platform::setApplication (ApplicationPtr&& app)
{
	application = std::move (app);
}

//------------------------------------------------------------------------
WindowPtr Platform::createWindow (const std::string& title, Size size, bool resizeable,
                                  const WindowControllerPtr& controller)
{
	return Window::make (title, size, resizeable, controller, hInstance);
}

//------------------------------------------------------------------------
void Platform::quit ()
{
	if (quitRequested)
		return;
	quitRequested = true;

	for (auto& window : Window::getWindows ())
		window->closeImmediately ();

	if (application)
		application->terminate ();

	PostQuitMessage (0);
}

//------------------------------------------------------------------------
void Platform::kill (int resultCode, const std::string& reason)
{
	auto str = StringConvert::convert (reason);
	MessageBox (nullptr, reinterpret_cast<LPCWSTR> (str.data ()), nullptr, MB_OK);
	exit (resultCode);
}

//------------------------------------------------------------------------
FUnknown* Platform::getPluginFactoryContext ()
{
	return nullptr;
}

//------------------------------------------------------------------------
void Platform::run (LPWSTR lpCmdLine, HINSTANCE _hInstance)
{
	hInstance = _hInstance;
	std::vector<std::string> cmdArgStrings;
	int numArgs = 0;
	auto cmdArgsArray = CommandLineToArgvW (lpCmdLine, &numArgs);
	cmdArgStrings.reserve (numArgs);
	for (int i = 0; i < numArgs; ++i)
	{
		cmdArgStrings.push_back (StringConvert::convert (Steinberg::wscast (cmdArgsArray[i])));
	}
	LocalFree (cmdArgsArray);

	auto noHIDPI = std::find (cmdArgStrings.begin (), cmdArgStrings.end (), "-noHIDPI");
	if (noHIDPI == cmdArgStrings.end ())
		ShcoreLibrary::instance ().setProcessDpiAwareness (true);

	application->init (cmdArgStrings);

	MSG msg;
	while (GetMessage (&msg, nullptr, 0, 0))
	{
		TranslateMessage (&msg);
		DispatchMessage (&msg);
	}
}

//------------------------------------------------------------------------
} // EditorHost
} // Vst
} // Steinberg

#ifndef _In_
#define _In_
#endif
#ifndef _In_opt_
#define _In_opt_
#endif

//------------------------------------------------------------------------
int APIENTRY wWinMain (_In_ HINSTANCE instance, _In_opt_ HINSTANCE /*prevInstance*/,
                       _In_ LPWSTR lpCmdLine, _In_ int /*nCmdShow*/)
{
	HRESULT hr = CoInitialize (nullptr);
	if (FAILED (hr))
		return FALSE;

	Steinberg::Vst::EditorHost::Platform::instance ().run (lpCmdLine, instance);

	CoUninitialize ();

	return 0;
}
