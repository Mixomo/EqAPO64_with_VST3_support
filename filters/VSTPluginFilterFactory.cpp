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
#include "helpers/StringHelper.h"
#include "helpers/VSTPluginLibrary.h"
#include "helpers/LogHelper.h"
#include "VSTPluginFilter.h"
#include "OutProcVSTPluginFilter.h"
#include "OutProcVSTPluginFilterFactory.h"
#include "FilterEngine.h"
#include "VSTPluginFilterFactory.h"

using namespace std;

vector<IFilter*> VSTPluginFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
    if(command!=L"VSTPlugin")return {};
    // Both configuration spellings use one DSP engine and the user's vendor
    // profile. Do not load any commercial module into audiodg for discovery.
    OutProcVSTPluginFilterFactory common;
    common.initialize(engine);
    std::wstring routedCommand=L"OutProcVSTPlugin";
    return common.createFilter(configPath,routedCommand,parameters);
}
