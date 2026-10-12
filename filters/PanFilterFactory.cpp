#include "stdafx.h"
#include <sstream>
#include "helpers/MemoryHelper.h"
#include "helpers/StringHelper.h"
#include "PanFilter.h"
#include "PanFilterFactory.h"

using namespace std;

vector<IFilter*> PanFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	if (command != L"Pan")
		return vector<IFilter*>();

	double position = 0.0;
	double width = 100.0;
	vector<wstring> parts = StringHelper::splitQuoted(StringHelper::replaceCharacters(parameters, L",", L"."), ' ');
	for (unsigned i = 0; i + 1 < parts.size(); i += 2)
	{
		if (parts[i] == L"Position")
			position = wcstod(parts[i + 1].c_str(), NULL);
		else if (parts[i] == L"Width")
			width = wcstod(parts[i + 1].c_str(), NULL);
	}

	void* mem = MemoryHelper::alloc(sizeof(PanFilter));
	return vector<IFilter*>(1, new(mem) PanFilter(position, width));
}
