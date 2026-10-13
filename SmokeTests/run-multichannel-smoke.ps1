param()
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root '_build\smoke'
$results=Join-Path $root ('output\smoke\multichannel-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$hostExe=Join-Path $root 'x64\Release\EqApoOutProcHost.exe'
if(!(Test-Path -LiteralPath $hostExe) -or !(Test-Path -LiteralPath (Join-Path $root 'x64\Release\Common.lib'))){throw 'Compile the application with build.bat first'}
New-Item -ItemType Directory -Force $results | Out-Null
function Check-Exit([string]$step){if($LASTEXITCODE -ne 0){throw "$step failed ($LASTEXITCODE). Results: $results"}}
& cmake -S (Join-Path $PSScriptRoot 'engine') -B $build -A x64 *> (Join-Path $results 'configure.log')
Check-Exit 'Configure multichannel harnesses'
& cmake --build $build --config Release --target EqApoAsymmetricChannels EqApoAsymmetricFixture EqApoRoutingFixture *> (Join-Path $results 'build.log')
Check-Exit 'Build multichannel harnesses'
$release=Join-Path $build 'Release'
Get-ChildItem -LiteralPath (Join-Path $root 'third_party\vcpkg_installed\x64-windows\bin') -Filter '*.dll' | Copy-Item -Destination $release -Force
$harness=Join-Path $release 'EqApoAsymmetricChannels.exe'
$decoder=Join-Path $build 'VST3\Release\EqApoAsymmetricFixture.vst3\Contents\x86_64-win\EqApoAsymmetricFixture.vst3'
$routing=Join-Path $build 'VST3\Release\EqApoRoutingFixture.vst3\Contents\x86_64-win\EqApoRoutingFixture.vst3'
& $harness $decoder $hostExe (Join-Path $results 'decoder.json') *> (Join-Path $results 'decoder.log')
Check-Exit 'Stereo decoder and independent worker PCM'
& python (Join-Path $PSScriptRoot 'engine\test_flexible_channel_layouts.py') --harness $harness --fixture $routing --host $hostExe --output (Join-Path $results 'layouts') *> (Join-Path $results 'layouts.log')
Check-Exit 'Flexible and fixed input/output layouts'
$decoderResult=Get-Content -LiteralPath (Join-Path $results 'decoder.json') -Raw | ConvertFrom-Json
$layoutResults=Get-Content -LiteralPath (Join-Path $results 'layouts\summary.json') -Raw | ConvertFrom-Json
$caseCount=$decoderResult.cases+($layoutResults | Measure-Object -Property cases -Sum).Sum
@{status='PASS';cases=$caseCount;deviceChannels=@(2,6,8);installedDeviceValidated=$false;physicalSpeakersValidated=$false} | ConvertTo-Json | Set-Content (Join-Path $results 'run-result.json') -Encoding UTF8
Write-Host "Results: $results. Virtual SDK/worker PCM validation; no installed-device changes."
