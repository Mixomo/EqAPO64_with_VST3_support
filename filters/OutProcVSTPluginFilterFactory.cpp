/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "stdafx.h"

#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "helpers/StringHelper.h"
#include "helpers/VSTPluginLibrary.h"
#include "FilterEngine.h"
#include "OutProcVSTPluginFilter.h"
#include "OutProcVSTPluginFilterFactory.h"

using namespace std;

void OutProcVSTPluginFilterFactory::initialize(FilterEngine* engine)
{
	this->engine = engine;
}

vector<IFilter*> OutProcVSTPluginFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	OutProcVSTPluginFilter* filter = NULL;

	if (command == L"OutProcVSTPlugin")
	{
		wstring libPath;
		wstring chunkData;
		wstring hostId;
		int vst3ClassIndex = 0;
		unordered_map<wstring, float> paramMap;
		vector<wstring> parts = StringHelper::splitQuoted(parameters, ' ');
		for (unsigned i = 0; i + 1 < parts.size(); i += 2)
		{
			wstring key = parts[i];
			wstring value = parts[i + 1];

			if (key == L"Library")
			{
				if (PathIsRelativeW(value.c_str()))
				{
					wchar_t filePath[MAX_PATH];
					wstring pluginPath = VSTPluginLibrary::getDefaultPluginPath();
					pluginPath._Copy_s(filePath, sizeof(filePath) / sizeof(wchar_t), MAX_PATH);
					if (pluginPath.size() < MAX_PATH)
						filePath[pluginPath.size()] = L'\0';
					else
						filePath[MAX_PATH - 1] = L'\0';
					PathAppendW(filePath, value.c_str());
					libPath = filePath;
				}
				else
					libPath = value;
			}
			else if (key == L"ChunkData")
				chunkData = value;
			else if (key == L"HostId")
				hostId = value;
			else if (key == L"ClassIndex")
				vst3ClassIndex = _wtoi(value.c_str());
			else if (key == L"Engine")
			{
				// Reserved for compatibility with experimental editor output.
			}
			else
			{
				if (!isdigit(value.c_str()[0]))
				{
					int x = i + 2;
					if (x <= parts.size())
					{
						float f = wcstof(parts[x].c_str(), NULL);
						paramMap[value.c_str()] = f;
					}
				}
				else
				{
					float f = wcstof(value.c_str(), NULL);
					paramMap[key] = f;
				}
			}
		}

		if (!libPath.empty())
		{
			TraceF(L"Adding out-of-process VST plugin %s", libPath.c_str());
			void* mem = MemoryHelper::alloc(sizeof(OutProcVSTPluginFilter));
			filter = new(mem) OutProcVSTPluginFilter(libPath, chunkData, paramMap, hostId, engine != nullptr && engine->isAnalysisMode(), vst3ClassIndex);
		}
	}

	if (filter == NULL)
		return vector<IFilter*>(0);
	return vector<IFilter*>(1, filter);
}
