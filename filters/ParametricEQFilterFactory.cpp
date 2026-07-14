#include "stdafx.h"
#include <algorithm>
#include <cwctype>
#include <regex>

#include "helpers/MemoryHelper.h"
#include "helpers/StringHelper.h"
#include "ParametricEQFilterFactory.h"

using namespace std;

static wregex regexBand(L"^\\s*(ON|OFF)?\\s*(PK|PEQ|LSQ|HSQ|LS|HS)\\s+Fc\\s*([-+0-9.eE]+)\\s*H?z?\\s+Gain\\s*([-+0-9.eE]+)\\s*dB\\s+Q\\s*([-+0-9.eE]+)", regex_constants::icase);

vector<ParametricEQFilter::Band> ParametricEQFilterFactory::parseBands(const wstring& parameters)
{
	vector<ParametricEQFilter::Band> bands;
	wstring normalized = StringHelper::replaceCharacters(parameters, L",", L".");
	vector<wstring> segments = StringHelper::split(normalized, L';');
	for (wstring segment : segments)
	{
		wsmatch match;
		if (!regex_search(segment, match, regexBand))
			continue;

		ParametricEQFilter::Band band;
		wstring enabled = match.str(1);
		transform(enabled.begin(), enabled.end(), enabled.begin(), towupper);
		band.enabled = enabled != L"OFF";

		wstring type = match.str(2);
		transform(type.begin(), type.end(), type.begin(), towupper);
		if (type == L"LS" || type == L"LSQ")
			band.type = BiQuad::LOW_SHELF;
		else if (type == L"HS" || type == L"HSQ")
			band.type = BiQuad::HIGH_SHELF;
		else
			band.type = BiQuad::PEAKING;

		band.freq = wcstod(match.str(3).c_str(), NULL);
		band.gain = wcstod(match.str(4).c_str(), NULL);
		band.q = wcstod(match.str(5).c_str(), NULL);
		if (band.freq > 0.0 && band.q > 0.0)
			bands.push_back(band);
	}
	return bands;
}

vector<IFilter*> ParametricEQFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	if (command != L"ParametricEQ")
		return vector<IFilter*>();

	vector<ParametricEQFilter::Band> bands = parseBands(parameters);
	void* mem = MemoryHelper::alloc(sizeof(ParametricEQFilter));
	return vector<IFilter*>(1, new(mem) ParametricEQFilter(bands));
}
