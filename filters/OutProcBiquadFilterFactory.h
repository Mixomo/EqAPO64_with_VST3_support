/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <map>
#include <string>

#include "BiQuad.h"
#include "IFilterFactory.h"
#include "IFilter.h"

class OutProcBiquadFilterFactory : public IFilterFactory
{
public:
	OutProcBiquadFilterFactory();
	std::vector<IFilter*> createFilter(const std::wstring& configPath, std::wstring& command, std::wstring& parameters) override;

private:
	double getFreq(const std::wstring& freqString);

	std::map<std::wstring, BiQuad::Type> filterNameToTypeMap;
	std::map<BiQuad::Type, std::wstring> filterTypeToDescriptionMap;
};
