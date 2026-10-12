/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "stdafx.h"
#define _USE_MATH_DEFINES
#include <cmath>
#include <regex>
#include <sstream>

#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "helpers/StringHelper.h"
#include "OutProcBiquadFilter.h"
#include "OutProcBiquadFilterFactory.h"

using namespace std;

static wregex regexType(L"^\\s*ON\\s+([A-Za-z]+)");
static wregex regexFreq(L"\\s+Fc\\s*([-+0-9.eE\u00A0]+)\\s*H\\s*z");
static wregex regexGain(L"\\s+Gain\\s*([-+0-9.eE]+)\\s*dB");
static wregex regexQ(L"\\s+Q\\s*([-+0-9.eE]+)");
static wregex regexBW(L"\\s+BW\\s+Oct\\s*([-+0-9.eE]+)");
static wregex regexSlope(L"^\\s*([-+0-9.eE]+)\\s*dB");

OutProcBiquadFilterFactory::OutProcBiquadFilterFactory()
{
	filterNameToTypeMap[L"PK"] = BiQuad::PEAKING;
	filterNameToTypeMap[L"PEQ"] = BiQuad::PEAKING;
	filterNameToTypeMap[L"Modal"] = BiQuad::PEAKING;
	filterNameToTypeMap[L"LP"] = BiQuad::LOW_PASS;
	filterNameToTypeMap[L"HP"] = BiQuad::HIGH_PASS;
	filterNameToTypeMap[L"LPQ"] = BiQuad::LOW_PASS;
	filterNameToTypeMap[L"HPQ"] = BiQuad::HIGH_PASS;
	filterNameToTypeMap[L"BP"] = BiQuad::BAND_PASS;
	filterNameToTypeMap[L"LS"] = BiQuad::LOW_SHELF;
	filterNameToTypeMap[L"HS"] = BiQuad::HIGH_SHELF;
	filterNameToTypeMap[L"LSC"] = BiQuad::LOW_SHELF;
	filterNameToTypeMap[L"HSC"] = BiQuad::HIGH_SHELF;
	filterNameToTypeMap[L"NO"] = BiQuad::NOTCH;
	filterNameToTypeMap[L"AP"] = BiQuad::ALL_PASS;

	filterTypeToDescriptionMap[BiQuad::PEAKING] = L"peaking";
	filterTypeToDescriptionMap[BiQuad::LOW_PASS] = L"low-pass";
	filterTypeToDescriptionMap[BiQuad::HIGH_PASS] = L"high-pass";
	filterTypeToDescriptionMap[BiQuad::BAND_PASS] = L"band-pass";
	filterTypeToDescriptionMap[BiQuad::LOW_SHELF] = L"low-shelf";
	filterTypeToDescriptionMap[BiQuad::HIGH_SHELF] = L"high-shelf";
	filterTypeToDescriptionMap[BiQuad::NOTCH] = L"notch";
	filterTypeToDescriptionMap[BiQuad::ALL_PASS] = L"all-pass";
}

vector<IFilter*> OutProcBiquadFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	OutProcBiquadFilter* filter = NULL;

	if (command == L"OutProcBiquad")
	{
		parameters = StringHelper::replaceCharacters(parameters, L",", L".");

		wsmatch match;
		if (regex_search(parameters, match, regexType))
		{
			wstring typeString = match.str(1);
			if (filterNameToTypeMap.find(typeString) != filterNameToTypeMap.end())
			{
				BiQuad::Type type = filterNameToTypeMap[typeString];
				wstring typeDescription = filterTypeToDescriptionMap[type];
				parameters = match.suffix().str();

				double freq = 0.0;
				double gain = 0.0;
				double bandwidthOrQOrS = 0.0;
				bool isBandwidthOrS = false;
				bool isCornerFreq = false;
				bool error = false;

				if (regex_search(parameters, match, regexFreq))
					freq = getFreq(match.str(1));
				else
				{
					LogF(L"No frequency given in OutProcBiquad string %s%s", typeString.c_str(), parameters.c_str());
					error = true;
				}

				if (regex_search(parameters, match, regexGain))
				{
					if (!(type == BiQuad::LOW_PASS || type == BiQuad::HIGH_PASS || type == BiQuad::NOTCH || type == BiQuad::ALL_PASS))
						gain = wcstod(match.str(1).c_str(), NULL);
				}
				else if (type == BiQuad::PEAKING || type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
				{
					LogF(L"No gain given in OutProcBiquad string %s%s", typeString.c_str(), parameters.c_str());
					error = true;
				}

				if (regex_search(parameters, match, regexQ))
					bandwidthOrQOrS = wcstod(match.str(1).c_str(), NULL);

				if (regex_search(parameters, match, regexBW))
				{
					if (!(type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF))
					{
						bandwidthOrQOrS = wcstod(match.str(1).c_str(), NULL);
						isBandwidthOrS = true;
					}
				}

				if (regex_search(parameters, match, regexSlope))
				{
					if (type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
					{
						bandwidthOrQOrS = wcstod(match.str(1).c_str(), NULL);
						isBandwidthOrS = true;
					}
				}

				if (bandwidthOrQOrS == 0.0)
				{
					if (type == BiQuad::PEAKING || type == BiQuad::ALL_PASS)
					{
						LogF(L"No Q or bandwidth given in OutProcBiquad string %s%s", typeString.c_str(), parameters.c_str());
						error = true;
					}
					else if (type == BiQuad::LOW_PASS || type == BiQuad::HIGH_PASS || type == BiQuad::BAND_PASS)
						bandwidthOrQOrS = M_SQRT1_2;
					else if (type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
					{
						bandwidthOrQOrS = 0.9;
						isBandwidthOrS = true;
					}
					else if (type == BiQuad::NOTCH)
						bandwidthOrQOrS = 30.0;
				}
				else if (type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
				{
					if (isBandwidthOrS)
						bandwidthOrQOrS /= 12.0;
					if (typeString[typeString.length() - 1] != L'C')
						isCornerFreq = true;
				}

				if (!error)
				{
					TraceF(L"OutProcBiquad: adding %s filter with frequency %g Hz, gain %g dB and parameter %g",
						typeDescription.c_str(), freq, gain, bandwidthOrQOrS);

					void* mem = MemoryHelper::alloc(sizeof(OutProcBiquadFilter));
					filter = new(mem) OutProcBiquadFilter(type, gain, freq, bandwidthOrQOrS, isBandwidthOrS, isCornerFreq);
				}
			}
			else if (typeString != L"None")
				LogF(L"Invalid OutProcBiquad filter type %s", typeString.c_str());
		}
	}

	if (filter == NULL)
		return vector<IFilter*>(0);
	return vector<IFilter*>(1, filter);
}

double OutProcBiquadFilterFactory::getFreq(const wstring& freqString)
{
	double result;
	wstring s = StringHelper::replaceCharacters(freqString, L"\u00A0", L"");
	int matched = swscanf_s(s.c_str(), L"%lf", &result);
	if (matched == 1)
	{
		if (s.length() >= 5 && s.find_first_of(L"eE") == wstring::npos && s[s.length() - 4] == L'.')
			result *= 1000.0;
		return result;
	}
	return -1.0;
}
