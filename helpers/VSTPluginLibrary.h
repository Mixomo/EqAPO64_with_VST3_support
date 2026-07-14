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

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "AbstractLibrary.h"
#include "aeffectx.h"
#include "pluginterfaces/base/ipluginbase.h"
#include <queue>
#include "VSTPluginInstance.h"

class VSTPluginLibrary : public AbstractLibrary
{
public:
	static std::shared_ptr<VSTPluginLibrary> getInstance(const std::wstring& libPath);
	static std::wstring getDefaultPluginPath();

	std::wstring getLibPath() override;
	std::wstring getLoadPath() override;
	bool isVST3() const;

	typedef vst_effect_t* (* vstPluginMain)(vst_host_callback_t audioMaster);
	vstPluginMain VSTPluginMain;
	typedef Steinberg::IPluginFactory* (PLUGIN_API* getPluginFactoryFunc)();
	getPluginFactoryFunc GetPluginFactory;
	Steinberg::IPluginFactory* getFactory() const;
	const Steinberg::PClassInfo& getVST3ClassInfo(int index = 0) const;
	int getVST3ClassCount() const;

protected:
	bool loadFunctions() override;
	int customInitialize() override;

private:
	VSTPluginLibrary(const std::wstring& libPath);
	static std::wstring resolveVST3ModulePath(const std::wstring& libPath);
	static std::unordered_map<std::wstring, std::weak_ptr<VSTPluginLibrary>> instanceMap;
	static std::wstring defaultPluginPath;
	std::wstring libPath;
	std::wstring loadPath;
	bool vst3 = false;
	Steinberg::IPluginFactory* factory = NULL;
	Steinberg::PClassInfo vst3ClassInfo;
	std::vector<Steinberg::PClassInfo> vst3ClassInfos;
};
