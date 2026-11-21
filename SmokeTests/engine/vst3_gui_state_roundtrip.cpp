// Reproduces the VST3 GUI edit path: the plugin's controller changes first,
// then the processor receives the edit only through queued parameter changes.
// The state must survive save -> fresh instance -> save -> fresh instance,
// both when audio is running and when the endpoint is idle (no process calls).
#include "stdafx.h"
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include "helpers/aeffectx.h"
#include "helpers/RealtimeVST.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
// The probe drives the plugin controller directly, as its own editor would.
#define private public
#include "helpers/VSTPluginInstance.h"
#undef private
#include "helpers/VSTPluginLibrary.h"
#include "outproc/OutProcVSTConfig.h"
#include "outproc/OutProcGuiObjects.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

static void check(bool value, const std::string& text) { if (!value) throw std::runtime_error(text); }

static double controllerValue(VSTPluginInstance& effect, unsigned id)
{
	for (const auto& p : effect.getParameterMetadata()) if (p.id == id) return p.normalizedValue;
	throw std::runtime_error("parameter missing");
}

static void processBlocks(VSTPluginInstance& effect, int blocks)
{
	std::vector<std::vector<float>> in(2, std::vector<float>(480, 0.f)), out(2, std::vector<float>(480, 0.f));
	float* inputs[2] = {in[0].data(), in[1].data()};
	float* outputs[2] = {out[0].data(), out[1].data()};
	for (int i = 0; i < blocks; ++i) effect.processReplacing(inputs, outputs, 480);
}

static std::unique_ptr<VSTPluginInstance> fresh(const std::shared_ptr<VSTPluginLibrary>& library, const std::wstring& state,
	const std::unordered_map<std::wstring, float>& params, bool runAudio)
{
	auto effect = std::make_unique<VSTPluginInstance>(library, 2);
	check(effect->initialize(), "initialize fresh instance");
	effect->setUsedChannelCount(2);
	effect->prepareForProcessing(48000, 480, false);
	effect->writeToEffect(state, params);
	effect->startProcessing();
	if (runAudio) processBlocks(*effect, 8);
	return effect;
}

// Edit exactly like a native VST3 editor routed through the DSP mailbox.
static void guiEdit(VSTPluginInstance& effect, unsigned id, double value)
{
	effect.vst3Controller->setParamNormalized(id, value);
	effect.notifyParameterEdit(id, value);
	effect.onAutomate();
	effect.applyParameterChangeOnDsp(id, value, 0);
}


// Runs the real Editor GUI host on a saved preset, closes it like the Editor
// does and returns the preset the host wrote back.
static OutProcVSTConfig roundTripThroughGuiHost(const std::wstring& host, const OutProcVSTConfig& input)
{
	wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp);
	const std::wstring session = L"vst3-gui-roundtrip-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
	const std::wstring path = std::wstring(temp) + session + L".opvs";
	check(OutProcWriteVSTConfig(path, input), "write GUI host config");
	std::wstring command = L"\"" + host + L"\" --parent-pid " + std::to_wstring(GetCurrentProcessId()) + L" --gui --session " + session + L" --vst-config \"" + path + L"\"";
	STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
	check(CreateProcessW(host.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process) != FALSE, "start GUI host");
	HANDLE exitEvent = nullptr;
	for (int i = 0; i < 200 && !exitEvent; ++i) { exitEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, OutProcGuiObjectName(session, L"GuiExit").c_str()); if (!exitEvent) Sleep(50); }
	check(exitEvent != nullptr, "GUI host did not open its editor");
	Sleep(2500);
	SetEvent(exitEvent); CloseHandle(exitEvent);
	const bool exited = WaitForSingleObject(process.hProcess, 15000) == WAIT_OBJECT_0;
	if (!exited) TerminateProcess(process.hProcess, 1);
	CloseHandle(process.hThread); CloseHandle(process.hProcess);
	check(exited, "GUI host did not exit");
	OutProcVSTConfig output;
	check(OutProcReadVSTConfig(path, output), "read GUI host config");
	DeleteFileW(path.c_str());
	return output;
}

int wmain(int argc, wchar_t** argv)
{
	if (argc < 2) { std::wcerr << L"usage: plugin.vst3 [paramCount]\n"; return 2; }
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	int failures = 0;
	try {
		auto library = VSTPluginLibrary::getInstance(argv[1]);
		check(library->initialize() >= 0, "load plugin");
		check(library->isVST3(), "VST3 plugin expected");
		const int maxEdits = argc > 2 ? _wtoi(argv[2]) : 6;
		for (int audio = 0; audio < 2; ++audio) {
			const bool runAudio = audio == 1;
			auto source = std::make_unique<VSTPluginInstance>(library, 2);
			check(source->initialize(), "initialize");
			source->setUsedChannelCount(2);
			source->prepareForProcessing(48000, 480, false);
			source->writeToEffect(L"", {});
			source->startProcessing();
			if (runAudio) processBlocks(*source, 4);

			std::vector<std::pair<unsigned, double>> edits;
			for (const auto& p : source->getParameterMetadata()) {
				if ((p.flags & ParameterInfo::kIsReadOnly) || !(p.flags & ParameterInfo::kCanAutomate)) continue;
				if (p.flags & (ParameterInfo::kIsBypass | ParameterInfo::kIsProgramChange)) continue;
				double target = p.normalizedValue < .5 ? .8 : .2;
				if (p.stepCount > 0) target = std::round(target * p.stepCount) / p.stepCount;
				if (std::abs(target - p.normalizedValue) < 1e-3) continue;
				guiEdit(*source, p.id, target);
				edits.push_back({p.id, source->vst3Controller->getParamNormalized(p.id)});
				if (int(edits.size()) >= maxEdits) break;
			}
			check(!edits.empty(), "no editable parameters");
			if (runAudio) processBlocks(*source, 4);

			std::wstring state; std::unordered_map<std::wstring, float> params;
			source->readFromEffect(state, params);
			// Editor stores only ChunkData when present.
			auto first = fresh(library, state, {}, runAudio);
			std::wstring state2; std::unordered_map<std::wstring, float> params2;
			first->readFromEffect(state2, params2);
			auto second = fresh(library, state2, {}, runAudio);
			// Third generation without any audio between save/restore.
			std::wstring state3; std::unordered_map<std::wstring, float> params3;
			second->readFromEffect(state3, params3);
			auto third = fresh(library, state3, {}, false);
			std::wstring state4; std::unordered_map<std::wstring, float> params4;
			third->readFromEffect(state4, params4);
			auto fourth = fresh(library, state4, {}, true);
			std::unique_ptr<VSTPluginInstance> viaGui, viaGuiTwice;
			if (argc > 3) {
				OutProcVSTConfig config; config.libraryPath = argv[1]; config.chunkData = state; config.paramMap = {};
				const auto once = roundTripThroughGuiHost(argv[3], config);
				viaGui = fresh(library, once.chunkData, {}, true);
				const auto twice = roundTripThroughGuiHost(argv[3], once);
				viaGuiTwice = fresh(library, twice.chunkData, {}, true);
			}

			for (const auto& edit : edits) {
				const double values[] = {controllerValue(*first, edit.first), controllerValue(*second, edit.first),
					controllerValue(*third, edit.first), controllerValue(*fourth, edit.first)};
				std::vector<double> all(values, values + 4);
				if (viaGui) { all.push_back(controllerValue(*viaGui, edit.first)); all.push_back(controllerValue(*viaGuiTwice, edit.first)); }
				for (int g = 0; g < int(all.size()); ++g) if (std::abs(all[g] - edit.second) > 1e-4) {
					++failures;
					std::wcout << L"FAIL audio=" << runAudio << L" generation=" << (g + 1) << L" id=" << edit.first
						<< L" expected=" << edit.second << L" actual=" << all[g] << L"\n";
				}
			}
			std::wcout << L"audio=" << runAudio << L" edits=" << edits.size() << L" checked\n";
		}
	}
	catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
	if (failures) { std::wcout << L"FAIL " << failures << L" values lost\n"; return 1; }
	std::wcout << L"PASS VST3 GUI-edit state round trip\n";
	return 0;
}
