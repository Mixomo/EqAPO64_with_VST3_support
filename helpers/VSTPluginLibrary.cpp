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
#include "RegistryHelper.h"
#include "LogHelper.h"
#include "VSTPluginLibrary.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

using namespace std;

std::unordered_map<std::wstring, std::weak_ptr<VSTPluginLibrary>> VSTPluginLibrary::instanceMap;
std::wstring VSTPluginLibrary::defaultPluginPath;

std::shared_ptr<VSTPluginLibrary> VSTPluginLibrary::getInstance(const wstring& libPath)
{
	shared_ptr<VSTPluginLibrary> ptr;

	auto it = instanceMap.find(libPath);
	if (it != instanceMap.end())
	{
		weak_ptr<VSTPluginLibrary> instance = it->second;
		ptr = instance.lock();
	}

	if (ptr == NULL)
	{
		ptr = shared_ptr<VSTPluginLibrary>(new VSTPluginLibrary(libPath));
		instanceMap[libPath] = ptr;
	}

	return ptr;
}

wstring VSTPluginLibrary::getDefaultPluginPath()
{
	if (defaultPluginPath == L"")
	{
		wstring installPath = RegistryHelper::readValue(APP_REGPATH, L"InstallPath");
		defaultPluginPath = installPath + L"\\VSTPlugins";
	}

	return defaultPluginPath;
}

std::wstring VSTPluginLibrary::getLibPath()
{
	return libPath;
}

std::wstring VSTPluginLibrary::getLoadPath()
{
	return loadPath;
}

bool VSTPluginLibrary::isVST3() const
{
	return vst3;
}

Steinberg::IPluginFactory* VSTPluginLibrary::getFactory() const
{
	return factory;
}

const Steinberg::PClassInfo& VSTPluginLibrary::getVST3ClassInfo(int index) const
{
	if (!vst3ClassInfos.empty())
	{
		if (index < 0 || index >= static_cast<int>(vst3ClassInfos.size()))
			index = 0;
		return vst3ClassInfos[index];
	}
	return vst3ClassInfo;
}

int VSTPluginLibrary::getVST3ClassCount() const
{
	return static_cast<int>(vst3ClassInfos.size());
}

bool VSTPluginLibrary::loadFunctions()
{
	if (vst3)
	{
		GetPluginFactory = (getPluginFactoryFunc)GetProcAddress(module, "GetPluginFactory");
		return GetPluginFactory != NULL;
	}

	VSTPluginMain = (vstPluginMain)GetProcAddress(module, "VSTPluginMain");
	return VSTPluginMain != NULL;
}

int VSTPluginLibrary::customInitialize()
{
	if (!vst3)
		return 0;

	factory = GetPluginFactory();
	if (factory == NULL)
		return FUNCTIONS_MISSING;

	for (Steinberg::int32 i = 0; i < factory->countClasses(); i++)
	{
		Steinberg::PClassInfo info;
		if (factory->getClassInfo(i, &info) == Steinberg::kResultOk
			&& strcmp(info.category, kVstAudioEffectClass) == 0)
		{
			if (vst3ClassInfos.empty())
				vst3ClassInfo = info;
			vst3ClassInfos.push_back(info);
		}
	}

	return vst3ClassInfos.empty() ? FUNCTIONS_MISSING : 0;
}

VSTPluginLibrary::VSTPluginLibrary(const wstring& libPath)
	: libPath(libPath), loadPath(libPath)
{
	wchar_t extension[_MAX_EXT];
	_wsplitpath_s(libPath.c_str(), NULL, 0, NULL, 0, NULL, 0, extension, _MAX_EXT);
	vst3 = _wcsicmp(extension, L".vst3") == 0;
	if (vst3)
		loadPath = resolveVST3ModulePath(libPath);
}

wstring VSTPluginLibrary::resolveVST3ModulePath(const wstring& libPath)
{
	DWORD attributes = GetFileAttributesW(libPath.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		return libPath;

#if defined(_M_ARM64)
	const wchar_t* platformDir = L"arm64-win";
#elif defined(_WIN64)
	const wchar_t* platformDir = L"x86_64-win";
#else
	const wchar_t* platformDir = L"x86-win";
#endif

	wchar_t searchPath[MAX_PATH];
	wcscpy_s(searchPath, libPath.c_str());
	PathAppendW(searchPath, L"Contents");
	PathAppendW(searchPath, platformDir);
	PathAppendW(searchPath, L"*.vst3");

	WIN32_FIND_DATAW findData;
	HANDLE hFind = FindFirstFileW(searchPath, &findData);
	if (hFind == INVALID_HANDLE_VALUE)
		return libPath;

	wchar_t modulePath[MAX_PATH];
	wcscpy_s(modulePath, libPath.c_str());
	PathAppendW(modulePath, L"Contents");
	PathAppendW(modulePath, platformDir);
	PathAppendW(modulePath, findData.cFileName);
	FindClose(hFind);

	return modulePath;
}
