#include "stdafx.h"
#include "helpers/MemoryHelper.h"
#include "helpers/StringHelper.h"
#include "ReverbFilter.h"
#include "ReverbFilterFactory.h"

using namespace std;

vector<IFilter*> ReverbFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	if (command != L"Reverb")
		return vector<IFilter*>();

	double roomSize = 50.0, damping = 50.0, wet = 20.0, dry = 100.0, width = 100.0;
	vector<wstring> parts = StringHelper::splitQuoted(StringHelper::replaceCharacters(parameters, L",", L"."), ' ');
	for (unsigned i = 0; i + 1 < parts.size(); i += 2)
	{
		if (parts[i] == L"RoomSize")
			roomSize = wcstod(parts[i + 1].c_str(), NULL);
		else if (parts[i] == L"Damping")
			damping = wcstod(parts[i + 1].c_str(), NULL);
		else if (parts[i] == L"Wet")
			wet = wcstod(parts[i + 1].c_str(), NULL);
		else if (parts[i] == L"Dry")
			dry = wcstod(parts[i + 1].c_str(), NULL);
		else if (parts[i] == L"Width")
			width = wcstod(parts[i + 1].c_str(), NULL);
	}

	void* mem = MemoryHelper::alloc(sizeof(ReverbFilter));
	return vector<IFilter*>(1, new(mem) ReverbFilter(roomSize, damping, wet, dry, width));
}
