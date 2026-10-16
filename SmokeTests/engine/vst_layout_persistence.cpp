#include <windows.h>
#include <wincrypt.h>
#include "helpers/VSTPluginInstance.h"
#include "helpers/VSTPluginLibrary.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <cstring>

static void check(bool value, const char* message)
{
	if (!value) throw std::runtime_error(message);
}

static std::vector<char> decodeBase64(const std::wstring& encoded)
{
	DWORD size = 0;
	check(CryptStringToBinaryW(encoded.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr) != FALSE,
		"decode saved state length");
	std::vector<char> decoded(size);
	check(CryptStringToBinaryW(encoded.c_str(), 0, CRYPT_STRING_BASE64,
		reinterpret_cast<BYTE*>(decoded.data()), &size, nullptr, nullptr) != FALSE, "decode saved state");
	decoded.resize(size);
	return decoded;
}

static std::wstring encodeBase64(const std::vector<char>& decoded)
{
	DWORD size = 0;
	check(CryptBinaryToStringW(reinterpret_cast<const BYTE*>(decoded.data()), DWORD(decoded.size()),
		CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size) != FALSE, "encode old state length");
	std::vector<wchar_t> encoded(size);
	check(CryptBinaryToStringW(reinterpret_cast<const BYTE*>(decoded.data()), DWORD(decoded.size()),
		CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &size) != FALSE, "encode old state");
	return encoded.data();
}

int wmain(int argc, wchar_t** argv)
{
	try {
		check(argc == 2, "usage: EqApoVstLayoutPersistence <vst3-plugin>");
		auto library = VSTPluginLibrary::getInstance(argv[1]);
		check(library->initialize() >= 0, "fixture scan");
		auto source = std::make_unique<VSTPluginInstance>(library, 4);
		check(source->initialize(), "initialize source instance");
		source->setUsedChannelCount(8);
		std::vector<Steinberg::Vst::SpeakerArrangement> inputs, outputs;
		check(source->getVST3BusArrangements(inputs, outputs) && !inputs.empty() && !outputs.empty(), "query negotiated layouts");
		inputs[0] = Steinberg::Vst::SpeakerArr::k71Proximity;
		outputs[0] = Steinberg::Vst::SpeakerArr::k71Proximity;
		check(source->setVST3BusArrangements(inputs, outputs), "select noncanonical 7.1 arrangement");
		std::wstring state;
		std::unordered_map<std::wstring, float> parameters;
		source->readFromEffect(state, parameters);
		check(!state.empty(), "persist VST3 state envelope");
		auto restored = std::make_unique<VSTPluginInstance>(library, 4);
		check(restored->initialize(), "initialize restore instance");
		restored->setUsedChannelCount(8);
		restored->writeToEffect(state, parameters);
		std::vector<Steinberg::Vst::SpeakerArrangement> restoredInputs, restoredOutputs;
		check(restored->getVST3BusArrangements(restoredInputs, restoredOutputs)
			&& restoredInputs == inputs && restoredOutputs == outputs,
			"fresh instance restores exact 7.1 layout rather than canonical 7.1 Music");

		// Version 3 saved component/controller chunks, UID and parameters, but
		// had no bus-layout fields. Rebuild that envelope shape to verify old
		// user configurations still load and default to the negotiated layout.
		auto currentBytes = decodeBase64(state);
		const std::string currentMagic = "EAPOVST3STATE4\n", oldMagic = "EAPOVST3STATE3\n";
		check(currentBytes.size() > currentMagic.size() + 32
			&& std::memcmp(currentBytes.data(), currentMagic.data(), currentMagic.size()) == 0,
			"identify version 4 state envelope");
		size_t layoutOffset = currentMagic.size() + 32;
		for (unsigned direction = 0; direction < 2; ++direction) {
			check(layoutOffset + sizeof(uint32_t) <= currentBytes.size(), "read saved bus count");
			uint32_t buses = 0;
			std::memcpy(&buses, currentBytes.data() + layoutOffset, sizeof(buses));
			layoutOffset += sizeof(buses);
			check(buses <= 64 && layoutOffset + size_t(buses) * sizeof(uint64_t) <= currentBytes.size(), "skip saved bus layouts");
			layoutOffset += size_t(buses) * sizeof(uint64_t);
		}
		std::vector<char> oldBytes(oldMagic.begin(), oldMagic.end());
		oldBytes.insert(oldBytes.end(), currentBytes.begin() + currentMagic.size(), currentBytes.begin() + currentMagic.size() + 32);
		oldBytes.insert(oldBytes.end(), currentBytes.begin() + layoutOffset, currentBytes.end());
		const auto oldState = encodeBase64(oldBytes);
		auto oldRestored = std::make_unique<VSTPluginInstance>(library, 4);
		check(oldRestored->initialize(), "initialize version 3 restore instance");
		oldRestored->setUsedChannelCount(8);oldRestored->writeToEffect(oldState, parameters);
		check(oldRestored->wasStateRestored(), "accept prior version 3 state envelope");
		std::vector<Steinberg::Vst::SpeakerArrangement> oldInputs, oldOutputs;
		check(oldRestored->getVST3BusArrangements(oldInputs, oldOutputs)
			&& oldInputs[0] == Steinberg::Vst::SpeakerArr::k71Music
			&& oldOutputs[0] == Steinberg::Vst::SpeakerArr::k71Music,
			"prior state envelope keeps its host-negotiated layout behavior");
		std::cout << "PASS VST3 same-channel custom speaker-layout persistence across instance restore\n";
		std::cout << "PASS existing VST3 version 3 state envelope compatibility\n";
		return 0;
	}
	catch (const std::exception& error) {
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
