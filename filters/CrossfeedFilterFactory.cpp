#include "stdafx.h"
#include <sstream>
#include "helpers/MemoryHelper.h"
#include "helpers/StringHelper.h"
#include "CrossfeedFilter.h"
#include "CrossfeedFilterFactory.h"

using namespace std;

vector<IFilter*> CrossfeedFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	if (command != L"Crossfeed")
		return vector<IFilter*>();

	wstring algorithm = L"Natural";
	double amount = 30.0;
	double circumference = 57.0;
	double headWidth = 15.0;
	double headLength = 19.0;
	double angle = 60.0;
	double cutoff = 700.0;
	double direct = 100.0;
	vector<wstring> parts = StringHelper::splitQuoted(StringHelper::replaceCharacters(parameters, L",", L"."), ' ');
	for (unsigned i = 0; i + 1 < parts.size(); i++)
	{
		if (parts[i] == L"Algorithm")
			algorithm = parts[i + 1];
		else if (parts[i] == L"Amount")
			amount = wcstod(parts[i + 1].c_str(), NULL);
		else if (parts[i] == L"Circumference")
			circumference = wcstod(parts[i + 1].c_str(), NULL);
		else if (parts[i] == L"HeadWidth")
			headWidth = wcstod(parts[i + 1].c_str(), NULL);
		else if (parts[i] == L"Width")
			headWidth = wcstod(parts[i + 1].c_str(), NULL);
		else if (parts[i] == L"HeadLength")
			headLength = wcstod(parts[i + 1].c_str(), NULL);
		else if (parts[i] == L"Length")
			headLength = wcstod(parts[i + 1].c_str(), NULL);
		else if (parts[i] == L"Angle")
			angle = wcstod(parts[i + 1].c_str(), NULL);
		else if (parts[i] == L"Cutoff")
			cutoff = wcstod(parts[i + 1].c_str(), NULL);
		else if (parts[i] == L"Direct")
			direct = wcstod(parts[i + 1].c_str(), NULL);
	}

	void* mem = MemoryHelper::alloc(sizeof(CrossfeedFilter));
	return vector<IFilter*>(1, new(mem) CrossfeedFilter(algorithm, amount, circumference, headWidth, headLength, angle, cutoff, direct));
}
