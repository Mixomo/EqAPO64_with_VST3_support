param(
    [ValidateRange(1,60)][int]$SmokeSeconds=60,
    [ValidateSet('Core','Transport','All')][string]$Suite='All',
    [string]$MatrixPath='',
    [switch]$WithFixtureGui,
    [string]$EditorPath=''
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root '_build\smoke'
$results=Join-Path $root ('output\smoke\'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$vstHostPath=Join-Path $root 'x64\Release\EqApoOutProcHost.exe'
if($EditorPath){
    if(!$WithFixtureGui -or !(Test-Path -LiteralPath $EditorPath -PathType Leaf)){throw 'Use -WithFixtureGui and an existing EditorPath with its Qt runtime deployed.'}
    $EditorPath=(Resolve-Path -LiteralPath $EditorPath).Path
}
if (!(Test-Path -LiteralPath $vstHostPath) -or !(Test-Path -LiteralPath (Join-Path $root 'x64\Release\Common.lib'))) {
    throw 'First compile the application with build.bat / build.ps1.'
}
if ($MatrixPath) {
    $matrix=Get-Content -LiteralPath $MatrixPath -Raw | ConvertFrom-Json
    if (!@($matrix.plugins).Count) { throw 'The plugin matrix must contain at least one entry.' }
    foreach ($entry in $matrix.plugins) {
        if (!$entry.path -or $entry.path -match '<|>' -or !(Test-Path -LiteralPath $entry.path)) {
            throw "Replace matrix placeholders with existing plugin paths: $($entry.path)"
        }
    }
}
New-Item -ItemType Directory -Path $results -Force | Out-Null
function Check-Exit([string]$step) { if ($LASTEXITCODE -ne 0) { throw "$step failed ($LASTEXITCODE). Results: $results" } }
& cmake -S (Join-Path $PSScriptRoot 'engine') -B $build -A x64 *> (Join-Path $results 'configure.log')
Check-Exit 'Configure harnesses'
$targets=@('EqApoRealtimeTests','EqApoContractTests','EqApoTransportSmoke','EqApoVstSmoke','EqApoFixture','EqApoFixtureVST2',
	'EqApoVstLayoutPersistence',
    'EqApoOutProcConfigTests','EqApoGuiObjectTests','EqApoConfigurationWriterTests','EqApoMonitorMailboxTests',
    'EqApoParameterMailboxTests','EqApoControlEndpointTests','EqApoHandleInheritanceTests','EqApoBrokerStreamScopeTests','EqApoPanelIdleTests',
    'EqApoAsymmetricFixture','EqApoAsymmetricChannels','EqApoRoutingFixture','EqApoPanelVisibility',
    'EqApoStatePersistence','EqApoBrokerStatePersistence')
& cmake --build $build --config Release --target @targets *> (Join-Path $results 'build.log')
Check-Exit 'Build harnesses'
$release=Join-Path $build 'Release'
$transport=Join-Path $build 'run-latest'
foreach ($destination in @($release,$transport)) {
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -LiteralPath $vstHostPath -Destination $destination -Force
    Get-ChildItem -LiteralPath (Join-Path $root 'third_party\vcpkg_installed\x64-windows\bin') -Filter '*.dll' |
        Copy-Item -Destination $destination -Force
}
Copy-Item -LiteralPath (Join-Path $root 'third_party\Qt\6.10.1\msvc2022_64\bin\Qt6Core.dll') -Destination $release -Force
Get-ChildItem -LiteralPath (Join-Path $root 'third_party\Qt\6.10.1\msvc2022_64\bin') -Filter 'icu*.dll' | Copy-Item -Destination $release -Force
$fixture=Join-Path $build 'VST3\Release\EqApoFixture.vst3\Contents\x86_64-win\EqApoFixture.vst3'
if ($Suite -in @('Core','All')) {
    $asymmetric=Join-Path $build 'VST3\Release\EqApoAsymmetricFixture.vst3\Contents\x86_64-win\EqApoAsymmetricFixture.vst3'
    & (Join-Path $release 'EqApoAsymmetricChannels.exe') $asymmetric $vstHostPath (Join-Path $results 'asymmetric-channels.json') *> (Join-Path $results 'asymmetric-channels.log')
    Check-Exit 'Asymmetric surround buses and worker PCM'
    $routing=Join-Path $build 'VST3\Release\EqApoRoutingFixture.vst3\Contents\x86_64-win\EqApoRoutingFixture.vst3'
    & python (Join-Path $PSScriptRoot 'engine\test_flexible_channel_layouts.py') --harness (Join-Path $release 'EqApoAsymmetricChannels.exe') --fixture $routing --host $vstHostPath --output (Join-Path $results 'flexible-channel-layouts') *> (Join-Path $results 'flexible-channel-layouts.log')
    Check-Exit 'Independent and fixed plugin channel layouts'
    & python (Join-Path $PSScriptRoot 'engine\test_scan_diagnostics.py') --host $vstHostPath --plugin $fixture *> (Join-Path $results 'scan-diagnostics.log')
    Check-Exit 'Isolated scanner diagnostics'
    foreach ($target in @('EqApoRealtimeTests','EqApoOutProcConfigTests','EqApoGuiObjectTests','EqApoConfigurationWriterTests',
        'EqApoMonitorMailboxTests','EqApoParameterMailboxTests','EqApoControlEndpointTests','EqApoHandleInheritanceTests','EqApoBrokerStreamScopeTests')) {
        & (Join-Path $release "$target.exe") *> (Join-Path $results "$target.log")
        Check-Exit $target
    }
    & (Join-Path $release 'EqApoContractTests.exe') $fixture --builtin *> (Join-Path $results 'contracts.log')
    Check-Exit 'Core and built-in contracts'
    & (Join-Path $release 'EqApoVstLayoutPersistence.exe') $fixture *> (Join-Path $results 'vst3-bus-layout-persistence.log')
    Check-Exit 'VST3 custom bus layout persistence'
    if ($WithFixtureGui) {
        & (Join-Path $release 'EqApoPanelIdleTests.exe') $fixture *> (Join-Path $results 'panel-idle.log')
        Check-Exit 'Retained graph panel animation'
        foreach ($panelFixture in @($fixture,(Join-Path $release 'EqApoFixtureVST2.dll'))) {
            $label=if ($panelFixture -eq $fixture) {'vst3'} else {'vst2'}
            & (Join-Path $release 'EqApoPanelVisibility.exe') $panelFixture *> (Join-Path $results "panel-visibility-$label.log")
            Check-Exit "Panel visibility $label"
            $parameter=if ($label -eq 'vst3') {7} else {0}
            & (Join-Path $release 'EqApoStatePersistence.exe') $panelFixture $parameter *> (Join-Path $results "state-persistence-$label.log")
            Check-Exit "State persistence $label"
        }
        & (Join-Path $release 'EqApoBrokerStatePersistence.exe') $vstHostPath $fixture (Join-Path $release 'EqApoFixtureVST2.dll') $fixture 7 0.75 *> (Join-Path $results 'broker-state-persistence.log')
        Check-Exit 'Multi-plugin broker state persistence'
        if($EditorPath){
            & python (Join-Path $PSScriptRoot 'engine\test_editor_stream_persistence.py') --editor $EditorPath --host $vstHostPath --probe (Join-Path $release 'EqApoBrokerStatePersistence.exe') --fixture-vst3 $fixture --fixture-vst2 (Join-Path $release 'EqApoFixtureVST2.dll') --output $results *> (Join-Path $results 'editor-stream-persistence.log')
            Check-Exit 'Actual Editor sessions and cold audio streams'
            $saveCloseOutput=Join-Path $results 'editor-save-on-close'
            $closeCase=Start-Process -FilePath $EditorPath -ArgumentList @('--save-on-close-audit',('"'+$saveCloseOutput+'"')) -WindowStyle Hidden -PassThru
            if(!$closeCase.WaitForExit(15000)){$closeCase.Kill();$closeCase.WaitForExit();throw "Editor close audit timed out; owned PID $($closeCase.Id)"}
            if($closeCase.ExitCode -ne 0){throw "Editor close audit failed ($($closeCase.ExitCode))"}
        }
    }
    $smokeArgs=@('--plugin',$fixture,'--out',(Join-Path $results 'fixture-vst3.json'))
    if ($WithFixtureGui) { $smokeArgs += '--gui' }
    & (Join-Path $release 'EqApoVstSmoke.exe') @smokeArgs
    Check-Exit 'VST3 fixture'
    $hadFixtureFault=Test-Path Env:\EQAPO_FIXTURE_FAULT
    $previousFixtureFault=$env:EQAPO_FIXTURE_FAULT
    try {
        $env:EQAPO_FIXTURE_FAULT='io-layout-once'
        $layoutArgs=@('--plugin',$fixture,'--out',(Join-Path $results 'fixture-vst3-io-restart-layout.json'),'--iterations','100')
        & (Join-Path $release 'EqApoVstSmoke.exe') @layoutArgs *> (Join-Path $results 'fixture-vst3-io-restart-layout.log')
        $layoutExit=$LASTEXITCODE
    }
    finally {
        if($hadFixtureFault){$env:EQAPO_FIXTURE_FAULT=$previousFixtureFault}else{Remove-Item Env:\EQAPO_FIXTURE_FAULT -ErrorAction SilentlyContinue}
    }
    if($layoutExit -ne 0){throw "VST3 layout retention after plugin I/O restart failed ($layoutExit). Results: $results"}
    $smokeArgs=@('--plugin',(Join-Path $release 'EqApoFixtureVST2.dll'),'--out',(Join-Path $results 'fixture-vst2.json'))
    if ($WithFixtureGui) { $smokeArgs += '--gui' }
    & (Join-Path $release 'EqApoVstSmoke.exe') @smokeArgs
    Check-Exit 'VST2 fixture'
}
if ($Suite -in @('Transport','All')) {
    & (Join-Path $transport 'EqApoTransportSmoke.exe') $fixture (Join-Path $results 'transport.json') $SmokeSeconds --oracle '#7' 0.25
    Check-Exit 'Transport timeline'
    if ($SmokeSeconds -ge 25) {
        & (Join-Path $transport 'EqApoTransportSmoke.exe') $fixture (Join-Path $results 'recovery.json') $SmokeSeconds --oracle '#7' 0.25 --faults
        Check-Exit 'Transport recovery'
    }
}
if ($MatrixPath) {
    & python (Join-Path $PSScriptRoot 'engine\run_plugin_matrix.py') --matrix $MatrixPath --smoke (Join-Path $release 'EqApoVstSmoke.exe') --output (Join-Path $results 'plugins') --timeout $SmokeSeconds
    Check-Exit 'Plugin matrix'
    $rows=Get-Content -LiteralPath (Join-Path $results 'plugins\matrix-results.json') -Raw | ConvertFrom-Json
    if (@($rows | Where-Object status -ne 'PASS').Count) { throw 'Plugin matrix has failed or blocked entries.' }
}
@{suite=$Suite; smokeSeconds=$SmokeSeconds; matrix=$MatrixPath; completed=$true; audiodgValidated=$false} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $results 'run-result.json') -Encoding UTF8
Write-Host "Results: $results. Harness checks only; no audiodg/device certification."
