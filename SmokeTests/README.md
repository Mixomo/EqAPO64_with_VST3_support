# Smoke Tests

These tools check the VST engine, control transport, editor behavior and installer
data handling. They use owned reference plug-ins or explicitly supplied external
plug-ins. Paths in `templates/` are placeholders to complete for your machine.

## Running The Master

1. Build the application with the root `build.bat` or `build.ps1`.
2. Make CMake 3.25+ available in PATH. Python 3 is required for Python helpers.
3. Open PowerShell in the project root and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\SmokeTests\run-smoke.ps1 -Suite All -SmokeSeconds 60
```

Alternatively, open `SmokeTests/run-smoke.bat`.

The master builds the required tools, uses the current host from `x64/Release/`,
then runs the selected checks. Results are written to `output/smoke/<timestamp>/`.

| Option | Purpose |
|---|---|
| `-Suite Core` | Runs contracts and VST2/VST3 fixture checks. |
| `-Suite Transport` | Runs the audio timeline and host-recovery scenarios. |
| `-Suite All` | Runs both suites; the default. |
| `-SmokeSeconds 60` | Sets transport duration and commercial plug-in timeout; range 1–60. |
| `-WithFixtureGui` | Includes fixture panel cycles and the retained-session meter, Hide/Show and reactivation regression. |
| `-MatrixPath '<YOUR_MATRIX_JSON>'` | Adds the configured external plug-in matrix. |

The whole suite can take longer than 60 seconds because it contains several
checks and a build. `SmokeSeconds` is not a total-suite deadline. Recovery is
skipped when the selected transport duration is below 25 seconds.

## Tests Run By The Master

Each row below states what the test checks and what a successful run is expected
to demonstrate. A failure stops the master and leaves the existing logs available.

### Core Contracts

| Test | What it tests | Expected result |
|---|---|---|
| `EqApoRealtimeTests` | Simulated asynchronous audio at 44.1/48/96 kHz, 1/2/6/8 channels and varied block sizes; latency alignment, graph crossfade readiness, missing-host recovery and callback allocations. | Correct delayed wet samples, finite output, aligned graphs and zero measured callback allocations; lost-host periods must not be reported as ready wet audio. |
| `EqApoOutProcConfigTests` | Configuration round-trip, truncation/corruption, nonfinite values and older format versions. | Valid state round-trips; invalid inputs are rejected without corrupting the previous configuration. |
| `EqApoGuiObjectTests` | Shared GUI objects in the interactive session without global-object privileges. | The GUI mapping can be created, opened and read correctly using the local namespace. |
| `EqApoConfigurationWriterTests` | Saving, temporary read locks, preservation on failure, change notification and file/folder permission handling. | Valid saves update the file; denied/locked writes return promptly with an error and preserve the previous contents. |
| `EqApoMonitorMailboxTests` | Bounded audio-input blocks, producer/consumer transfer, identity validation, overflow and generation refresh. | Correct samples and format arrive; wrong identities are rejected; overflow remains nonblocking and a replacement generation reconnects. |
| `EqApoParameterMailboxTests` | Concurrent parameter delivery, ordering, identities, offsets, generation ownership and overflow. | Accepted events are consumed consistently; stale generations cannot consume new events and full queues are handled explicitly. |
| `EqApoControlEndpointTests` | Named control IPC, permissions, plug-in identity, replacement/cleanup and native-view handoff. | Correct owners can communicate; incorrect identities are rejected and panel/control handoff preserves the intended generation. |
| `EqApoBrokerStreamScopeTests` | Worker identity across repeated configuration saves while bypassed, adjacent active generations, format changes and module removal. | A bypassed module reuses its compatible identity; active generations stay independent and removed/incompatible modules do not reuse it. |
| `EqApoPanelIdleTests` (with `-WithFixtureGui`) | A retained session stops receiving audio while its panel remains open; meter decay, hidden updates, resumed processing and panel closure. | The meter falls to zero and returns on resumed audio; the same worker/window survives Hide/Show, with no orphan after close. Output: `panel-idle.log`. |
| `EqApoHandleInheritanceTests` | Child-process inheritance of session handles. | The explicitly allowed handle reaches the child; unrelated handles do not. |
| `EqApoContractTests` | Built-in DSP contracts and VST3 fixture class/state/parameter behavior, automation offsets, malformed-state rejection and native scale/resize requests. | DSP matches its reference, state restores correctly, invalid classes/state are rejected and repeated scale changes do not accumulate size errors. |

The contract executable also supports additional VST2 persistence checks when a
VST2 fixture path is supplied as its extra argument. The master instead runs the
VST2 fixture through the general VST smoke below.

### VST2 And VST3 Fixture Smoke

The master invokes `EqApoVstSmoke` once for the owned VST3 fixture and once for the
owned VST2 fixture.

- **What it does:** loads and initializes the effect, processes a matrix of
  sample rates, channel requests, supported sample precision and changing blocks,
  sends parameter updates, then stores/restores state.
- **What it tests:** module/component lifecycle, I/O negotiation, valid processing,
  finite audio and parameter-state round-trip.
- **Expected:** all supported combinations finish without a crash or NaN/Inf;
  restored parameters match their saved values. Unsupported double precision is
  skipped according to the plug-in capability.
- **With `-WithFixtureGui`:** also exercises repeated panel attachment/detachment.
  Expected: supported panels open and close without damaging the effect instance.
  Inspect `gui_cycles`: a zero count does not demonstrate that the panel opened,
  even if the processing checks passed.
- **Output:** `fixture-vst2.json` and `fixture-vst3.json`.

### Audio Timeline

`EqApoTransportSmoke` processes the owned fixture with parameter `#7 = 0.25`.

- **What it tests:** real host transport, sequence handling, declared delay and a
  known wet-output gain, with finite output and callback-duration statistics.
- **Expected:** settled wet samples match the delayed sine-wave oracle with the
  expected gain; there is valid audio rather than a permanently dry or silent path.
- **Output:** `transport.json`.

### Host Recovery

The same transport tool runs with `--faults` when the duration is at least 25 seconds.

- **What it tests:** injected worker failures, aligned fallback, replacement and
  return to valid processing; the scenario includes repeated failures/quarantine.
- **Expected:** all planned fault injections occur, output remains finite and the
  wet oracle passes in the periods where processing is expected to be available.
- **Output:** `recovery.json`.

### External Plug-in Matrix

1. Copy `templates/plugin-matrix.example.json` to a local JSON file.
2. Replace the placeholder paths with actual public VST2/VST3 binaries.
3. Remove entries you do not intend to run.
4. Add `-MatrixPath '<YOUR_MATRIX_JSON>'` to the master command.

`run_plugin_matrix.py` runs the general VST smoke in a separate process per plug-in,
including its panel, with the configured timeout.

- **What it tests:** the same load/process/state/GUI checks reported by the smoke
  executable, for the specified external effects.
- **Expected:** exit code zero and a PASS report for each executable check. Missing
  paths are rejected by the master; a timeout or crash needs diagnosis.
- **Output:** individual logs/JSON and `plugins/matrix-results.json`.
- `required_checks` describes requested coverage; checks absent from the report
  are still unexecuted. Loading or a timeout alone does not establish license status.

## Optional C++ Diagnostics

These targets are preserved under `engine/` but are not all run by the master.
Build the target you need and use the argument contract in its source:

```powershell
cmake -S .\SmokeTests\engine -B .\_build\smoke -A x64
cmake --build .\_build\smoke --config Release --target <TARGET_NAME>
```

| Target | What it tests | Expected result |
|---|---|---|
| `EqApoFilterProbe` / `EqApoFilterProbeV2` | Parameter changes through production filters with configurable processing/oracle settings. | Finite PCM and the specified before/after audio relationship. |
| `EqApoLiveParameterProbe` | Parameter delivery to a persistent processor, optionally paired with a panel. | The expected audio change reaches DSP and remains valid in the requested phases. |
| `EqApoLifecycleProbe` / `EqApoLifecycleProbeV2` | Repeated bypass, reload, unload and load while an independent PCM clock runs. | Every requested phase finishes; wet/dry RMS matches the oracle and settled regions contain no unexpected zero blocks. |
| `EqApoHostStartupProgressTests` | Deliberately slow initialization and initialization deadline handling. | The 6.5-second fixture starts without false stall/readiness; the 11-second fixture exceeds its deadline and exits as expected. |
| `EqApoStartupProbe` | Startup progress, readiness and consumed audio blocks. | A live host becomes ready, consumes blocks and produces finite audio. |
| `EqApoIdleHostProbe` | Idle worker stability and CPU use for the requested interval. | Worker remains stable and measured average CPU stays below the tool's threshold. |
| `EqApoModuleLoadAudit` | Time spent loading the DLL and initializing its VST component. | Valid stage timing data or an explicit load/initialization error. This is profiling, not an automatic performance PASS. |
| `EqApoPublicationStressProbe` | Graph publication while preparation/allocation is deliberately delayed. | Publication completes and PCM stays finite with the expected settled response. |
| `EqApoPendingControlProbe` | Edits while a replacement processor is deliberately slow to initialize. | Audible generation responds within the fixture threshold and the edited value survives replacement. |
| `EqApoConcurrentHostsProbe` | Two independently active hosts. | Both remain alive, consume blocks and preserve their separate expected audio results. |
| `EqApoBrokerInstanceTests` | Broker instance exclusivity, identities, concurrent requests and retired generations. | Requests remain unique; stale owners and mismatched generations are rejected. |
| `EqApoBrokerTransportProbe` | Secured broker audio/state communication after the starter exits, duplicate-worker rejection and shutdown/lease expiry. | Worker continues processing correctly, responds to edits and exits according to the requested ownership/lease scenario. |
| `EqApoBrokerRepeatedHandoffProbe` | Repeated state/generation handoff, optional overlapping streams and format grids. | Expected gain survives handoffs, generations remain distinct and worker reuse respects the selected scenario. |
| `EqApoBrokerEngineTransitionProbe` | FilterEngine replacement with live control and native-panel handoff. | The intended generation controls the audible PCM after transition; the panel remains connected. |
| `EqApoBrokerRetryPublicationProbe` | Saturated broker catalog and failed/retried publication. | Failure is reported explicitly and the graph can finish safely on finite dry audio. |
| `EqApoNativeCheckpointRecovery` | Native full-state checkpoints and recovery after an owned worker exits. | Checkpoint state is restored and the fixture produces the expected PCM. |
| `EqApoModuleBypassProbe` | Active → module bypass → active. | Expected wet/dry/wet levels return without nonfinite output or unexpected settled silence. |
| `EqApoModuleReactivationProbe` | Warm reactivation with a retained native panel. | Compatible worker PID and HWND survive; finite processing returns within the tool's warm-transition threshold. |
| `EqApoUnifiedVstEngineProbe` | VST2/VST3 unified routing and legacy command aliases across bypass and replacement. | State applies, bypass is dry, reactivation retains compatible worker/panel and concurrent active generations stay independent. |
| `EqApoAnalysisAudit` | AnalysisThread response, latency and request/result metadata. | Valid response data or an explicit analysis status for the supplied configuration; use a matching response oracle to assess correctness. |
| `EqApoAnalysisFailureProbe` | A failed analysis processor and subsequent requests. | Failure is explicit; later blocks fail quickly instead of repeating the full wait. |

Some targets place their executable in `run-latest` or `control-v6` rather than
`Release`. Use the output path shown by the build. Legacy INPROC/OUTPROC names in
diagnostics cover old contracts; the product exposes one VST mode.

## Optional Python Diagnostics

Run a helper with `--help` for its required paths and options. All external paths
must be supplied explicitly. Helpers that accept `--cases` use a completed copy
of `templates/probe-cases.example.json`; parameter IDs, gain scales and expected
responses must match the effect being checked.

### Audio, Parameters And Lifecycle

| Script | What it tests | Expected result |
|---|---|---|
| `run_filter_probes.py` | A matrix of production-filter probes for supplied effects. | Each executed probe meets its PCM contract; untested compatibility checks remain incomplete. |
| `run_lifecycle_probes.py` | Repeated lifecycle cycles split into bounded child-process batches. | All requested cycles complete with each batch passing its audio/lifecycle checks. |
| `run_native_dsp_view_batch.py` | Native panel attachment/detachment paired with PCM and fixture input-meter checks. | View attaches/detaches, DSP remains valid and the configured audio/meter oracle matches. |
| `run_rate_block_probes.py` | 44.1/48/96 kHz and multiple block sizes with configurable parameter edits. | Every requested combination produces the specified gain ratio with valid rate/block metadata. |
| `test_live_gui_pcm.py` | A native panel edit while a processor remains active. | Measured audio changes by the expected ratio and, when requested, stays correct after closing the panel. |
| `test_gui_state_bridge.py` | GUI changes published as generation/state updates. | Generation and persisted parameter values change correctly and the owned GUI exits cleanly. |

### Editor And Native Panels

| Script | What it tests | Expected result |
|---|---|---|
| `test_qt_plugin_dialog.py` | A Qt plug-in dialog's open/close and instance teardown. | The requested dialog lifecycle completes with no process failure. |
| `run_qt_dialog_audits.py` | Inventory-driven batches of the Qt dialog test. | Every selected dialog case passes. |
| `test_protected_row_close.py` | Closing a protected VST row, including optional fixture close faults. | Editor/row shutdown completes and the requested fixture lifecycle stages are observed. |
| `run_protected_close_batch.py` | Sequential batches of protected-row close scenarios. | All requested children finish within their bound and pass their close checks. |
| `test_editor_row_live.py` | Clicking the plug-in's native panel through the real editor row/dialog. | The parameter edit reaches processing and matches the supplied audio ratio. |
| `test_editor_row_release.py` | Repeated row lifetime/release cycles in owned editor processes. | Requested cycles finish, resources are released and no child reports a lifecycle failure. |
| `test_editor_documents.py` | Reopening sample configuration documents and inspecting stored/visible rows. | Expected lines and placeholders survive each fresh open. |
| `test_native_panel.py` | Native panel discovery, capture, optional click and shutdown. | The owned panel is found and captured, requested actions finish and shutdown succeeds; audio needs its paired PCM check. |
| `test_commercial_gui.py` | User-defined gestures/captures with simultaneous PCM observations. | Actions and observations are recorded and the process exits; the effect of a private button requires a suitable audio/state oracle. |

### Analyzer And Bundled IRs

| Script | What it tests | Expected result |
|---|---|---|
| `test_analysis_response.py` | Fixture gain/latency, text encoding, failure handling and optional configured response cases. | Analyzer data matches each specified response/latency or the intended failure status. |
| `test_analysis_revisions.py` | Replacing analysis requests while changing a parameter. | Final request/result revisions agree and the final response matches the latest requested gain. |
| `test_analysis_shapes.py` | Static response shapes selected using the effect's parameter metadata. | Measured response matches the configured shape oracle, not merely its center gain. |
| `test_bundled_ir_selection.py` | Bundled IR enumeration, editor selection/restoration and convolution analysis. | Each expected IR is available, selection restores and analyzed response matches its reference. |

### Installer Data And Result Aggregation

| Script | What it tests | Expected result |
|---|---|---|
| `test_installer_payloads.py` | Optional IR/calibration ownership/removal scripts on sample files. | Owned files follow the selected options; pre-existing, user-modified and user-created files/configurations remain intact. |
| `test_nsis_owned_payload.py` | Generated NSIS ownership includes and their stack/output-directory behavior. | Payload goes to the intended directory and the caller's variables/stack are preserved. |
| `test_nsis_feature_localization.py` | Feature strings, selected flags and language fallback in NSIS. | Labels/flags match the selected or fallback language with no reported expansion errors. |
| `build_compatibility_matrix.py` | Aggregates discovery and recorded checks into a coverage matrix. | Executed evidence is attached to the correct effect; absent checks are still marked incomplete rather than promoted to full compatibility. |

`case_inputs.py` validates the external-case JSON; it is shared input code, not a
standalone smoke. `fixture_vst2.cpp` and `fixture_vst3.cpp` are reference effects.
`CrashDiagnostics.h` collects diagnostic information; it is not a separate test.

## Reading Results

### Installed-device Panel Regression

Install Python packages `numpy` and `sounddevice` for this optional check:

```powershell
python -m pip install numpy sounddevice
python -m sounddevice
```

The second command lists local audio devices. Select a playback device and its
matching hardware/driver loopback input using their numeric indices, with a
sample rate supported by both. The test sends a low-level tone to that output.

`live_panel_regression.py` is an opt-in playback/loopback check after installing
the updated release. It takes explicit `--editor`, `--config`, `--case-file`,
`--output`, `--render-device` and `--loopback-device` arguments; `--seconds` is
limited to 30–60. The config must be a temporary test document selected for the
APO, not the user's normal configuration. Complete `templates/live-case.example.json`
with the desired effect, Windows user SID and a valid gain oracle, or use null
for `expected_gain` when that effect has no simple linear-gain expectation.

It checks visible bypass, Hide/Show while bypassed, repeated Preamp changes,
retention of a hidden panel, X-close and reopen, visible/hidden reactivation,
panel placement ahead of the editor, worker/window identity and real loopback
audio. The VST3 fixture also checks that its live output meter decays to silence
during bypass and returns after reactivation. Expected: one connected panel,
consistent button/visibility state, retained compatible worker, finite audio
and the specified gain relationship. Commercial-panel meter/FPS behavior requires
its own oracle and is not inferred from a window remaining open.

`invoke-live-editor.ps1` operates only on the specified owned editor process.
`prepare-installed-runtime.ps1` is an optional elevated helper for explicitly
coordinated temporary ConfigPath redirection/restoration. It does not install or
close applications; restarting Windows Audio requires its explicit `-RestartAudio`
switch. Use that helper only with a prepared settings file and live owner process.
Closing the owner or creating `runtime-stop` restores ConfigPath.

`run-live-smoke.ps1` coordinates those tools using explicit installed-root,
results-directory and audio-device arguments. Prepare `config/config.txt` and
one or more `*-case.json` files in the results directory first. Save and close
the Editor, pause other playback, and accept the Windows elevation prompt.
The runner temporarily redirects ConfigPath, restarts Windows Audio, runs each
case and verifies restoration of ConfigPath and the original configuration.
Place the results directory under `output/live-audio/`. Each case JSON follows
`templates/live-case.example.json`; its `line` must describe one VST, and `sid`
must be the signed-in Windows user's SID. Use `whoami /user` to obtain it.
Start the temporary configuration with `Preamp: 0 dB`. Each case adds its own
VST row and restores that temporary file afterward. Existing case result
directories are not overwritten; use a new results directory for another run.

Replace all placeholders in this example before running:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\SmokeTests\run-live-smoke.ps1 -ResultsDirectory '<PROJECT_ROOT>\output\live-audio\<RUN_NAME>' -InstalledRoot '<INSTALL_DIRECTORY>' -RenderDevice <PLAYBACK_DEVICE_INDEX> -LoopbackDevice <LOOPBACK_INPUT_INDEX> -SampleRate <SUPPORTED_SAMPLE_RATE> -Seconds 60
```

Each case writes `result.json` and `signal.npz`. Expected: `status` is `PASS`,
audio is finite and nonzero in the measured phases, panel/worker identity is
preserved where required, and any configured gain oracle matches. The fixture
also requires its meter to reach zero during bypass and return afterward.
`runtime-ready.json` identifies the installed binaries; `runtime-restored.json`
must confirm both ConfigPath restoration and unchanged original configuration.

With `-WithFixtureGui`, the core suite also runs `EqApoPanelIdleTests`. It keeps
an old audio session alive while stopping its audio requests, reproducing the
interval between module bypass and destruction of the previous graph. The
fixture meter must decay to zero, including with its panel hidden, and return
when processing resumes. The same window and worker must survive; closing the
panel must leave no orphan. This is an isolated-host regression, separate from
the installed-device loopback test.

Named broker tests use the broker currently running in the Windows user session.
Ensure it belongs to the build being tested; copying a new worker beside the
smoke executable does not replace an already running broker.

- Successful contract executables return zero. The smoke/probe JSON should also
  contain the expected PASS/true fields for the scenario being run.
- `configure.log` and `build.log` explain toolchain/build failures.
- Individual test logs and JSON explain the failed assertion or processing stage.
- `run-result.json` records completion of the master's selected suite.
- BLOCKED, missing results, omitted scenarios and observation-only captures do not
  count as a processing PASS.

The master does not install the APO or edit an installed configuration. Its audio
checks exercise the host/harness paths. Installer-data checks do not register APOs
or create restore points; GUI captures do not measure animation frame rates.
Installed-device audio and physical monitor behavior require their own checks.
