#include "stdafx.h"
#include "helpers/MemoryHelper.h"
#include "helpers/StringHelper.h"
#include "VUMeterFilter.h"
#include "VUMeterFilterFactory.h"

using namespace std;

vector<IFilter*> VUMeterFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	if (command != L"VUMeter")
		return vector<IFilter*>();

	wstring meterId = L"default";
	wstring channels = L"all";
	wstring rmsStandard = L"AES17";
	wstring lufsStandard = L"ITU-R BS.1770-5";
	vector<wstring> parts = StringHelper::splitQuoted(parameters, ' ');
	for (unsigned i = 0; i + 1 < parts.size(); i += 2)
	{
		if (parts[i] == L"MeterId")
			meterId = parts[i + 1];
		else if (parts[i] == L"Channels")
			channels = parts[i + 1];
		else if (parts[i] == L"RMS" || parts[i] == L"RMSStandard")
			rmsStandard = parts[i + 1];
		else if (parts[i] == L"LUFS" || parts[i] == L"LUFSStandard")
			lufsStandard = parts[i + 1];
	}

	void* mem = MemoryHelper::alloc(sizeof(VUMeterFilter));
	return vector<IFilter*>(1, new(mem) VUMeterFilter(meterId, channels, rmsStandard, lufsStandard));
}
