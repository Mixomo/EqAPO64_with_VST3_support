/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "stdafx.h"

#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "helpers/StringHelper.h"
#include "OutProcGainFilter.h"
#include "OutProcGainFilterFactory.h"

using namespace std;

vector<IFilter*> OutProcGainFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	OutProcGainFilter* filter = NULL;

	if (command == L"OutProcGain")
	{
		wstring value = StringHelper::replaceCharacters(parameters, L",", L".");

		double gainDb;
		int matched = swscanf_s(value.c_str(), L" %lf dB", &gainDb);
		if (matched == 1)
		{
			TraceF(L"OutProcGain: adjusting gain out-of-process by %g dB", gainDb);

			void* mem = MemoryHelper::alloc(sizeof(OutProcGainFilter));
			filter = new(mem) OutProcGainFilter(gainDb);
		}
	}

	if (filter == NULL)
		return vector<IFilter*>(0);
	return vector<IFilter*>(1, filter);
}
