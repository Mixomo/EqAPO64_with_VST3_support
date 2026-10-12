#pragma once

#include <cstdint>
#include <cmath>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

constexpr std::uint32_t OUTPROC_VST_CONFIG_MAGIC = 0x4F505653; // OPVS
constexpr std::uint32_t OUTPROC_VST_CONFIG_VERSION = 3;
constexpr std::uint32_t OUTPROC_VST_CONFIG_MAX_STRING_LENGTH = 64 * 1024 * 1024;
constexpr std::uint32_t OUTPROC_VST_CONFIG_MAX_PARAM_COUNT = 65536;

struct OutProcVSTConfig
{
	std::wstring libraryPath;
	int vst3ClassIndex = 0;
    std::wstring classUID;
	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
};

inline bool OutProcWriteString(std::ostream& stream, const std::wstring& value)
{
	if (value.size() > UINT32_MAX)
		return false;

	const std::uint32_t length = static_cast<std::uint32_t>(value.size());
	stream.write(reinterpret_cast<const char*>(&length), sizeof(length));
	if (length > 0)
		stream.write(reinterpret_cast<const char*>(value.data()), length * sizeof(wchar_t));
	return stream.good();
}

inline bool OutProcReadString(std::istream& stream, std::wstring& value)
{
	std::uint32_t length = 0;
	stream.read(reinterpret_cast<char*>(&length), sizeof(length));
	if (!stream.good())
		return false;
	if (length > OUTPROC_VST_CONFIG_MAX_STRING_LENGTH)
		return false;

	// The announced length is untrusted. Grow only after bytes have actually
	// arrived, so a four-byte truncated payload cannot reserve 128 MB.
	std::wstring decoded;
	wchar_t buffer[4096];
	std::uint32_t remaining = length;
	while (remaining)
	{
		const std::uint32_t count = remaining < 4096 ? remaining : 4096;
		stream.read(reinterpret_cast<char*>(buffer), count * sizeof(wchar_t));
		if (!stream.good()) return false;
		decoded.append(buffer, count);
		remaining -= count;
	}
	value.swap(decoded);
	return true;
}

inline bool OutProcWriteVSTConfigStream(std::ostream& stream, const OutProcVSTConfig& config)
{

	const std::uint32_t magic = OUTPROC_VST_CONFIG_MAGIC;
	const std::uint32_t version = OUTPROC_VST_CONFIG_VERSION;
	const std::uint32_t paramCount = static_cast<std::uint32_t>(config.paramMap.size());

	stream.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
	stream.write(reinterpret_cast<const char*>(&version), sizeof(version));
	if (!OutProcWriteString(stream, config.libraryPath))
		return false;
	stream.write(reinterpret_cast<const char*>(&config.vst3ClassIndex), sizeof(config.vst3ClassIndex));
	if (!OutProcWriteString(stream, config.classUID)) return false;
	if (!OutProcWriteString(stream, config.chunkData))
		return false;
	stream.write(reinterpret_cast<const char*>(&paramCount), sizeof(paramCount));

	for (const auto& entry : config.paramMap)
	{
		if (!OutProcWriteString(stream, entry.first))
			return false;
		stream.write(reinterpret_cast<const char*>(&entry.second), sizeof(entry.second));
	}

	return stream.good();
}

inline bool OutProcWriteVSTConfigPayload(const std::wstring& path, const OutProcVSTConfig& config)
{
    std::ofstream stream(path,std::ios::binary|std::ios::trunc);
    return stream && OutProcWriteVSTConfigStream(stream,config);
}

inline bool OutProcWriteVSTConfig(const std::wstring& path, const OutProcVSTConfig& config)
{
	const std::wstring tempPath = path + L".tmp." + std::to_wstring(GetCurrentProcessId());
	if (!OutProcWriteVSTConfigPayload(tempPath, config))
	{
		DeleteFileW(tempPath.c_str());
		return false;
	}

	if (!MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DeleteFileW(tempPath.c_str());
		return false;
	}

	return true;
}

inline bool OutProcReadVSTConfigStream(std::istream& stream, OutProcVSTConfig& destination)
{
	OutProcVSTConfig config;
	std::uint32_t magic = 0;
	std::uint32_t version = 0;
	stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
	stream.read(reinterpret_cast<char*>(&version), sizeof(version));
	if (!stream.good() || magic != OUTPROC_VST_CONFIG_MAGIC || (version < 1 || version > OUTPROC_VST_CONFIG_VERSION))
		return false;

	if (!OutProcReadString(stream, config.libraryPath))
		return false;
	config.vst3ClassIndex = 0;
	if (version >= 2)
	{
		stream.read(reinterpret_cast<char*>(&config.vst3ClassIndex), sizeof(config.vst3ClassIndex));
		if (!stream.good())
			return false;
	}
	config.classUID.clear();
    if(version>=3 && !OutProcReadString(stream,config.classUID)) return false;
	if (!OutProcReadString(stream, config.chunkData))
		return false;

	std::uint32_t paramCount = 0;
	stream.read(reinterpret_cast<char*>(&paramCount), sizeof(paramCount));
	if (!stream.good())
		return false;
	if (paramCount > OUTPROC_VST_CONFIG_MAX_PARAM_COUNT)
		return false;

	config.paramMap.clear();
	for (std::uint32_t i = 0; i < paramCount; ++i)
	{
		std::wstring key;
		float value = 0.0f;
		if (!OutProcReadString(stream, key))
			return false;
		stream.read(reinterpret_cast<char*>(&value), sizeof(value));
		if (!stream.good() || !std::isfinite(value))
			return false;
		config.paramMap[key] = value;
	}

	destination = std::move(config);
	return true;
}

inline bool OutProcReadVSTConfig(const std::wstring& path,OutProcVSTConfig& config)
{
    std::ifstream stream(path,std::ios::binary);
    return stream && OutProcReadVSTConfigStream(stream,config);
}
