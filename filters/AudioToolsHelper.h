#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace AudioTools
{
inline double dbToGain(double db)
{
	return std::pow(10.0, db / 20.0);
}

inline double percentToUnit(double value)
{
	return (std::max)(0.0, (std::min)(1.0, value / 100.0));
}

inline std::wstring toUpper(std::wstring value)
{
	for (wchar_t& ch : value)
		ch = towupper(ch);
	return value;
}

inline std::vector<std::wstring> splitChannelList(const std::wstring& value)
{
	std::vector<std::wstring> result;
	std::wstring current;
	for (wchar_t ch : value)
	{
		if (ch == L',' || ch == L';' || ch == L' ')
		{
			if (!current.empty())
			{
				result.push_back(toUpper(current));
				current.clear();
			}
		}
		else
		{
			current += ch;
		}
	}
	if (!current.empty())
		result.push_back(toUpper(current));
	return result;
}

inline std::vector<unsigned> resolveChannels(const std::wstring& selector, const std::vector<std::wstring>& channelNames)
{
	std::vector<unsigned> channels;
	std::wstring normalized = toUpper(selector);
	if (normalized.empty() || normalized == L"ALL")
	{
		for (unsigned i = 0; i < channelNames.size(); i++)
			channels.push_back(i);
		return channels;
	}

	if (normalized == L"MONO")
	{
		if (!channelNames.empty())
			channels.push_back(0);
		return channels;
	}

	if (normalized == L"STEREO")
	{
		const unsigned count = channelNames.size() < 2 ? static_cast<unsigned>(channelNames.size()) : 2u;
		for (unsigned i = 0; i < count; i++)
			channels.push_back(i);
		return channels;
	}

	std::vector<std::wstring> names = splitChannelList(normalized);
	for (const std::wstring& name : names)
	{
		for (unsigned i = 0; i < channelNames.size(); i++)
		{
			if (toUpper(channelNames[i]) == name)
			{
				channels.push_back(i);
				break;
			}
		}
	}

	std::sort(channels.begin(), channels.end());
	channels.erase(std::unique(channels.begin(), channels.end()), channels.end());
	return channels;
}

inline std::uint32_t fnv1a(const std::wstring& value)
{
	std::uint32_t hash = 2166136261u;
	for (wchar_t ch : value)
	{
		hash ^= static_cast<std::uint32_t>(ch);
		hash *= 16777619u;
	}
	return hash;
}
}
