#include "stdafx.h"
#include "helpers/MemoryHelper.h"
#include "helpers/StringHelper.h"
#include "ToneGeneratorFilter.h"
#include "ToneGeneratorFilterFactory.h"
#include "AudioToolsHelper.h"

using namespace std;

vector<IFilter*> ToneGeneratorFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	if (command != L"ToneGenerator")
		return vector<IFilter*>();

	bool state = true;
	ToneGeneratorFilter::Type type = ToneGeneratorFilter::SINE;
	ToneGeneratorFilter::Mode mode = ToneGeneratorFilter::REPLACE;
	double frequency = 1000.0, start = 20.0, end = 20000.0, duration = 10.0, level = -20.0;
	wstring channels = L"all";
	vector<wstring> parts = StringHelper::splitQuoted(StringHelper::replaceCharacters(parameters, L",", L"."), ' ');
	for (unsigned i = 0; i < parts.size(); i++)
	{
		wstring key = parts[i];
		if (key == L"Hz" || key == L"dB" || key == L"s")
			continue;
		if (i + 1 >= parts.size())
			break;
		wstring value = parts[++i];
		if (key == L"State")
			state = wcstol(value.c_str(), NULL, 10) != 0;
		else if (key == L"Type")
		{
			wstring v = AudioTools::toUpper(value);
			if (v == L"WHITE") type = ToneGeneratorFilter::WHITE;
			else if (v == L"PINK") type = ToneGeneratorFilter::PINK;
			else if (v == L"BROWN") type = ToneGeneratorFilter::BROWN;
			else if (v == L"SWEEP") type = ToneGeneratorFilter::SWEEP;
			else type = ToneGeneratorFilter::SINE;
		}
		else if (key == L"Frequency")
			frequency = wcstod(value.c_str(), NULL);
		else if (key == L"Start")
			start = wcstod(value.c_str(), NULL);
		else if (key == L"End")
			end = wcstod(value.c_str(), NULL);
		else if (key == L"Duration")
			duration = wcstod(value.c_str(), NULL);
		else if (key == L"Level")
			level = wcstod(value.c_str(), NULL);
		else if (key == L"Channels")
			channels = value;
		else if (key == L"Mode")
			mode = AudioTools::toUpper(value) == L"MIX" ? ToneGeneratorFilter::MIX : ToneGeneratorFilter::REPLACE;
	}

	void* mem = MemoryHelper::alloc(sizeof(ToneGeneratorFilter));
	return vector<IFilter*>(1, new(mem) ToneGeneratorFilter(state, type, frequency, start, end, duration, level, channels, mode));
}
