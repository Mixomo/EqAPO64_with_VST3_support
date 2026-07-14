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

#include "helpers/VSTPluginLibrary.h"
#include "helpers/StringHelper.h"
#include "filters/VSTPluginFilterFactory.h"
#include "filters/OutProcVSTPluginFilterFactory.h"
#include "filters/VSTPluginFilter.h"
#include "VSTPluginFilterGUI.h"
#include "VSTPluginFilterGUIFactory.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

using namespace std;

static std::wstring resolveVSTLibraryPath(const std::wstring& value)
{
	if (value.empty())
		return value;

	QString path = QString::fromStdWString(value);
	if (QDir::isRelativePath(path))
	{
		QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
		path = QFileInfo(pluginsDir, path).absoluteFilePath();
	}
	return QDir::toNativeSeparators(path).toStdWString();
}

QList<FilterTemplate> VSTPluginFilterGUIFactory::createFilterTemplates()
{
	QList<FilterTemplate> list;
	list.append(FilterTemplate(tr("VST plugin"), "VSTPlugin:", QStringList(tr("Plugins"))));
	list.append(FilterTemplate(tr("Out-of-process VST plugin"), "OutProcVSTPlugin:", QStringList(tr("Plugins"))));
	return list;
}

IFilterGUI* VSTPluginFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	VSTPluginFilterGUI* result = NULL;

	if (command == "VSTPlugin" || command == "OutProcVSTPlugin")
	{
		const bool outProcMode = command == "OutProcVSTPlugin";
		if (outProcMode)
		{
			std::wstring libPath;
			std::wstring chunkData;
			QString hostId;
			int vst3ClassIndex = 0;
			std::unordered_map<std::wstring, float> paramMap;
			std::vector<std::wstring> parts = StringHelper::splitQuoted(parameters.toStdWString(), ' ');
			for (unsigned i = 0; i + 1 < parts.size(); i += 2)
			{
				std::wstring key = parts[i];
				std::wstring value = parts[i + 1];
				if (key == L"Library")
					libPath = resolveVSTLibraryPath(value);
				else if (key == L"ChunkData")
					chunkData = value;
				else if (key == L"HostId")
					hostId = QString::fromStdWString(value);
				else if (key == L"ClassIndex")
					vst3ClassIndex = _wtoi(value.c_str());
				else if (key == L"Engine")
				{
					// Compatibility token for experimental lines.
				}
				else
				{
					float f = wcstof(value.c_str(), NULL);
					paramMap[key] = f;
				}
			}
			result = new VSTPluginFilterGUI(VSTPluginLibrary::getInstance(libPath), chunkData, paramMap, true, hostId, vst3ClassIndex);
		}
		else
		{
			VSTPluginFilterFactory factory;
			std::wstring commandWStr = command.toStdWString();
			std::wstring paramWStr = parameters.toStdWString();
			std::vector<IFilter*> filters = factory.createFilter(L"", commandWStr, paramWStr);
			if (!filters.empty())
			{
				VSTPluginFilter* filter = (VSTPluginFilter*)filters[0];
				result = new VSTPluginFilterGUI(filter->getLibrary(), filter->getChunkData(), filter->getParamMap(), false, QString(), filter->getVST3ClassIndex());
			}
			else
			{
				result = new VSTPluginFilterGUI(VSTPluginLibrary::getInstance(L""), L"", unordered_map<wstring, float>());
			}

			for (IFilter* f : filters)
			{
				f->~IFilter();
				MemoryHelper::free(f);
			}
		}
	}

	return result;
}
