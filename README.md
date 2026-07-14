# Equalizer APO 64 with VST3 support

<div align="center">
  <a href="https://sourceforge.net/projects/eqapo64-with-vst3-support/">
    <img src="assets/sourceforge_badge.png" alt="Sourceforge Badge 1" width="100">
  </a>
  &nbsp;&nbsp;
  <a href="https://sourceforge.net">
    <img src="assets/sourceforge_badge2.png" alt="Sourceforge Badge 2" width="100">
  </a>
</div>

This repository is a restructured Windows fork based on [TheFireKahuna/equalizerAPO64](https://github.com/TheFireKahuna/equalizerAPO64), with the goal of keeping the familiar Equalizer APO workflow while adding
native VST3 plug-in support.

![VST3_loader_with_CalCurve_vst3](assets/vst3_out_loader.png)

NOTE: This build was compiled for Windows 10/11 64 bits with AVX2 support only (More compatible with all CPUs). If you need AVX512 support, you'll need a compatible CPU and will have to compile it yourself from this repository. 

## Main Features

- Double-precision processing through the 64-bit internal pipeline for precision
  when applying multiple overlapping effects, including convolution, complex
  parametric EQ configurations and GraphicEQ curves.
- Native VST3 hosting through the Steinberg VST3 SDK.
- Existing VST2 support retained for older plug-ins.
- Experimental out-of-process VST hosting for isolating plug-ins from the
  Configuration Editor and the APO audio engine.
- Native pan, chorus, reverb, crossfeed, tone-generator and professional VU-meter modules.
- GraphicEQ FIR export and explicit IR/FIR sample-rate validation for
  convolution.
- Full parametric EQ module with unlimeted filters, import and export options.
- Full Headphone Calibration module with uses a bundled ASH catalog generated from the
  [ASH-Toolset](https://github.com/ShanonPearce/ASH-Toolset) project by Shanon
  Pearce.
- Bundled various Free Impulse Responses inside the Convolution module. (Credits to Greg Hopkins
  (Hopkins Media Services) and [Aleksey Vaneev](https://github.com/avaneev) from ([Voxengo](https://www.voxengo.com/)).)

- Configuration Editor workflow preserved.
- Reproducible installer build using local dependencies under `third_party/`.
- NSIS-based safe installer packaging for end users.

## Current VST3 Status

Like VST2, VST3 support is not universal, and there is no guarantee that it will work with all VST3 effects plugins on the market.
It is best to use it with simple, lightweight plugins, as some more complex ones may expose or require parameters that the APO pipeline does not expose or support. 

The Configuration Editor offers two VST-related paths:

- `VSTPlugin:` uses the original in-process loader.
- `OutProcVSTPlugin:` uses the experimental isolated host while keeping the
  original Equalizer APO VST loading code.

The analyzer state sync is intentionally conservative. VST2/VST3 plug-in state
is synchronized in roughly 500 ms intervals so analysis-only plug-ins do not
cause constant host refreshes.

## About VST3 Plug-ins

VST3 plug-ins can be distributed either as a single `.vst3` file or as a bundle
directory ending in `.vst3`. On Windows, many VST3 plug-ins store their actual
binary under a path similar to:

```text
PluginName.vst3/Contents/x86_64-win/PluginName.vst3
```

If a plug-in does not show its editor, does not animate, process the audio with artifacts or crashes when opened or removed, test it first in a standard VST3 host or DAW. Some plug-ins require host features that Equalizer APO does not provide.

## Headphone Calibration, Parametric EQ, VST And Installer Update - July 12, 2026 (Exp Branch)

This update focuses on making the Configuration Editor easier to use for
headphone correction, editable EQ work, plug-in compatibility and installer
diagnostics.

### Headphone Calibration

- Adds a new headphone-calibration module for creating correction filters from
  headphone correction data.
- Uses a bundled ASH catalog generated from the
  [ASH-Toolset](https://github.com/ShanonPearce/ASH-Toolset) project by Shanon
  Pearce.
- Organizes ASH entries by source, brand and model.
- Lets users send the selected correction directly to GraphicEQ, Convolution or
  the new Parametric Equalizer module.
- Adds export options for GraphicEQ CSV, FIR WAV and ParametricEQ text output.

### Parametric Equalizer

- Adds a new editable `ParametricEQ:` module with multiple filter bands in one
  row.
- Supports peak, low-shelf and high-shelf bands.
- Provides sliders and numeric fields for frequency, gain and Q.
- Allows adding, removing, sorting, importing, exporting and resetting bands.
- Double-clicking a band control returns it to useful neutral defaults:
  `1000 Hz`, `0 dB` and `Q 1.00`.

### Convolution And Included IRs

- Expands the Convolution filter workflow for bundled IR/FIR collections.
- Adds a clearer IR/FIR selection flow for packaged impulse responses.
- Keeps automatic sample-rate matching and normalization focused on making
  loaded FIR files safer to use.
- Bundled IRs are free to use, but their copyright belongs to Greg Hopkins
  (Hopkins Media Services) and [Aleksey Vaneev](https://github.com/avaneev) from ([Voxengo](https://www.voxengo.com/)).

### VST Plug-in Handling

- Adds VST3 class selection for plug-in bundles that expose more than one
  effect in the same `.vst3` package.
- Improves VST3 parameter transfer so GUI edits are reflected by the audio
  engine and analyzer more consistently, including the out-of-process loader.
- Adds an in-editor compatibility note reminding users to try both VST loaders,
  because some plug-ins behave better in one mode than the other.

### Installer Diagnostics

- Adds `install-diagnostics.log` when APO registration fails, making missing
  bundled runtime files easier to identify from user reports.
- Checks the installed APO, audio runtime, Visual C++ runtime and Qt runtime
  files as `present` or `missing` without trying to replace Windows system
  DLLs.
- If bundled DLLs are missing, the installer can ask the user before copying
  those local DLLs again and retrying APO registration.
- Keeps the actual `regsvr32` exit code in the log for cases where all bundled
  files are present but Windows still refuses registration.
- Runs an embedded 64-bit DLL load diagnostic on registration failure so the
  log can include the Windows loader error code and message for
  `EqualizerAPO.dll`.

## Native Tools And Editor Update - June 18, 2026 (Exp Branch)

The June 18, 2026 update focuses on keeping the native audio tools practical
and predictable inside the Configuration Editor while preserving the normal APO
signal path.

### Editor Workflow

- Adds reset actions to native filter rows that were missing a module-level
  reset control.
- Adds double-click reset behavior for slider and numeric controls where
  supported by the editor widgets.
- Keeps reset behavior local to the selected filter type, so parametric filters
  return to the defaults that match their current filter shape instead of a
  generic global default.
- Keeps clone, add, remove and edit controls visible in denser rows by
  improving row spacing and control placement.

### Native Crossfeed

- Adds a native `Crossfeed:` filter intended for headphone listening.
- Provides `Natural` and `BS2B` processing modes.
- Adds anatomy-based controls for head circumference, width and length, with
  average female and average male presets.
- Exposes the main crossfeed parameters through editor sliders so the filter
  can be tuned without hand-editing every token.

### VU Meter

- Improves the floating `VUMeter:` panel for multi-channel layouts.
- Keeps the numeric measurement area separate from the scrolling channel meter
  area, making surround devices easier to inspect.
- Adds LUFS/RMS standard selectors in the filter row, defaulting to `AES17`
  for RMS and `ITU-R BS.1770-5` for LUFS.
- Keeps the meter passive: it reports the signal at the position where the
  `VUMeter:` filter is inserted and does not alter audio.
  
## Stability And Workflow Update - June 5, 2026 (Exp Branch)

The June 5, 2026 update stabilizes the experimental out-of-process VST path and
polishes the Configuration Editor workflow around the new audio tools.

### Out-of-process VST Hosting

- Launches the out-of-process VST GUI host detached from `Editor.exe`, so
  closing the Configuration Editor no longer owns or terminates the matching
  `EqApoOutProcHost.exe` process.
- Closing the Configuration Editor no longer treats every filter row as a
  removed module. `OutProcVSTPlugin:` hosts are only terminated when the row is
  actually removed, the plug-in is changed, or the reload action is used.
- Keeps show/hide panel behavior separate from plug-in lifetime: hiding or
  closing the panel does not unload the plug-in, while removing the row still
  shuts down the matching host.
- Adds a reload action for VST rows so a plug-in can be explicitly restarted
  without manually deleting and recreating the filter.
- Preserves the conservative VST state synchronization model for editor,
  analyzer and audio-engine instances.

### Configuration Editor

- Adds clone-above and clone-below actions for filter rows.
- Improves filter-row spacing so add, clone, remove and edit controls remain
  visible in dense configurations.
- Fixes context-menu placement and submenu parenting issues that could cause
  menus to drift, resize inconsistently or appear away from the cursor.
- Keeps GraphicEQ controls easier to reach by widening the numeric table area
  and preventing old narrow preferences from collapsing it.
- Improves Qt runtime discovery when launching the editor from different working
  directories.

### Installer And Packaging

- Stages the detached `EqApoOutProcHost.exe` with the main x64 binaries.
- Ensures required Qt image/style/platform plug-ins are copied into the
  installer payload.
- Removes stale experimental JUCE host files during install/upgrade if they are
  present from previous local builds.

## Audio Tools And Convolution Update - June 3, 2026 (Exp Branch)

The June 3, 2026 update extends the experimental branch with native audio
tools, measurement features, FIR export and a safer convolution workflow. The
new filters use the same Equalizer APO pipeline as the existing filters and can
therefore be applied to both playback and capture devices when the APO is
installed on the selected endpoint.

### Native DSP Filters

- Adds a channel-aware `Pan:` filter for positioning and stereo-width control.
- Adds a native `Chorus:` filter with adjustable rate, depth, mix and feedback.
- Adds a lightweight native `Reverb:` filter with room size, damping, wet/dry
  balance and width controls.
- Provides practical slider controls for these filters instead of
  requiring users to edit every parameter manually.
- Supports applying the filters to all available channels or selected channels,
  including common surround layouts.

### Tone Generator

- Adds a native `ToneGenerator:` filter that generates audio directly inside
  the APO pipeline.
- Supports sine, white-noise, pink-noise, brown-noise and looping sine-sweep
  modes.
- Provides play/stop, level, frequency, sweep range, duration, channel
  selection and replace/mix controls.
- Can target all available channels or selected channels on playback and
  capture endpoints.

### Professional VU Meter

- Adds a pass-through `VUMeter:` filter that measures the real audio stream at
  its exact position in the configuration.
- Provides a resizable floating DAW-style meter panel from the filter row.
- Displays per-channel peak, peak hold, RMS, clipping and dB scales together
  with LUFS momentary, short-term and integrated measurements.
- Supports reset and channel-aware metering without altering the audio signal.
- Can meter microphone or other capture-device audio when placed in a
  `Stage: capture` section. It measures only audio that actually passes through
  Equalizer APO; WASAPI exclusive and ASIO streams that bypass APO are not
  visible to the meter.

### Convolution And FIR Workflow

- Fixes convolution memory handling and improves stability when audio block
  sizes change.
- Adds an internal HybridConv correctness benchmark covering 44.1, 48, 96 and
  192 kHz processing.
- Requires loaded IR/FIR files to match the current device sample rate. A
  mismatched IR/FIR is clearly reported and is not applied, preventing silent
  timbre changes caused by interpreting the impulse response at the wrong
  sample rate.
- Detects IR/FIR sample-rate mismatches immediately when a file is selected or
  its path is edited, using the live sample rate reported by the current audio
  endpoint.
- Adds a **Regenerate / create a sample-rate-matched IR/FIR for the current
  device** action beside the displayed sample rate. It extracts the loaded
  IR/FIR magnitude response and regenerates a new matched FIR for the current
  device sample rate.
- Adds FIR export from GraphicEQ at the selected device sample rate, providing
  the preferred path for creating native FIR files from custom EQ curves.

## Installer Update - June 1, 2026 (Exp Branch)

The June 1, 2026 experimental branch update is a full x64 installer refresh for
the VST3-capable fork:

- Adds the experimental `OutProcVSTPlugin:` path, which runs plug-ins in
  `EqApoOutProcHost.exe` so many plug-in crashes can be isolated from the
  Configuration Editor and the APO audio engine.
- Shows out-of-process VST editor windows with live GUI animation support for
  plug-ins that render animated meters, curves or visual feedback.
- Improves out-of-process panel lifecycle handling: show/hide does not unload
  the plug-in, removing the row shuts down the matching host process, and stale
  host sessions are cleaned up more reliably.
- Synchronizes VST state between the editor, audio engine and analyzer at a
  conservative interval so analyzer-only plug-ins do not continuously recreate
  host instances.
- Stores richer VST state for both VST2 and VST3 while remaining compatible
  with older `ChunkData` entries.
- Installs `EqApoOutProcHost.exe` next to the main Equalizer APO binaries.

## Safety And Recovery

This installer registers an Audio Processing Object with selected Windows audio
devices. If Windows reports missing runtime DLLs, or if the Windows Audio service
becomes unstable after installation, remove Equalizer APO from the selected audio
devices first:

1. Open Equalizer APO Device Selector from the Start menu.
2. Uncheck all selected playback and capture devices.
3. Apply the change and reboot Windows if requested.
4. Then uninstall Equalizer APO normally.

Before installation, the installer also attempts to create a Windows restore
point named `EqualizerAPO_<version>_PreInstall`. This is best-effort: Windows may
reject it when System Protection is disabled or another restore point was
recently created. The installer bundles the required x64 Visual C++ runtime DLLs
app-local and stops before modifying audio devices if the APO cannot be
registered.

## Installation

### Option A - Install From GitHub Releases

For normal users, install from the latest GitHub Release:

1. Download the x64 installer
2. Run the installer.
3. Choose the playback or capture devices that should use Equalizer APO.
4. Reboot Windows if the installer or Device Selector asks for it.
5. Open Configuration Editor and add filters as usual.
6. To use a plug-in, add a VST plug-in filter and select either a VST2 `.dll`
   or a VST3 `.vst3` bundle.

### Option B - Install From The `Setup` Directory

Also the generated installer is located here:

```text
Setup/EqualizerAPO-x64-1.4.2.exe
```

### Option C - Install From SourceForge

[![Download EqAPO64_with_VST3_support](https://a.fsdn.com/con/app/sf-download-button)](https://sourceforge.net/projects/eqapo64-with-vst3-support/files/latest/download)

## Building

The build is designed to be reproducible from the repository root. Third-party
dependencies are installed locally under `third_party/`, so a global Qt or NSIS
installation is not required.

### Build Requirements

- Windows 10 or Windows 11 x64.
- Visual Studio 2022 with the Desktop development with C++ workload.
- A compatible Windows SDK installed through Visual Studio.
- PowerShell 5 or newer.
- Git.
- CMake.
- Internet access for the first dependency bootstrap.

### One-command Installer Build

From the repository root:

```powershell
.\scripts\build-installer-x64.ps1 -Configuration Release
```

The script performs the complete packaging flow:

- Bootstraps local dependencies into `third_party/`.
- Builds the Equalizer APO x64 projects.
- Builds Qt-based applications such as Configuration Editor and Device Selector.
- Deploys the required Qt runtime files with `windeployqt`.
- Stages runtime files under `Setup/lib64`.
- Creates the final NSIS installer under `Setup/`.

## License Summary

- Equalizer APO is distributed under the GNU General Public License. This fork is
  intended to be distributed under GPLv3-or-later where permitted by the original
  upstream licensing terms.
- Keep the original Equalizer APO copyright and license notices when
  redistributing binaries or source code.
- Steinberg VST3 SDK: used for VST3 hosting. License under MIT terms at `third_party/vst3sdk`.
- Microsoft MSVC / Visual Studio C++ toolchain: https://microsoft.com/
- C++ language created by Bjarne Stroustrup.

