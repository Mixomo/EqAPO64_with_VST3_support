/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  Equalizer APO contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "stdafx.h"
#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "helpers/StringHelper.h"
#include "OutputGuardFilter.h"
#include "OutputGuardFilterFactory.h"

using namespace std;

vector<IFilter*> OutputGuardFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	(void)configPath;
	OutputGuardFilter* filter = NULL;

	if (command == L"OutputGuard")
	{
		wstring value = StringHelper::replaceCharacters(parameters, L",", L".");
		double ceilingDb = -1.0;
		int matched = swscanf_s(value.c_str(), L" %lf dB", &ceilingDb);
		if (matched == 1)
		{
			TraceF(L"Enabling output guard at %g dBFS", ceilingDb);

			void* mem = MemoryHelper::alloc(sizeof(OutputGuardFilter));
			filter = new(mem) OutputGuardFilter(ceilingDb);
		}
	}

	if (filter == NULL)
		return vector<IFilter*>(0);
	return vector<IFilter*>(1, filter);
}
