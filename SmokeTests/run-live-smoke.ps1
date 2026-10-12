param(
    [Parameter(Mandatory=$true)][string]$ResultsDirectory,
    [Parameter(Mandatory=$true)][string]$InstalledRoot,
    [Parameter(Mandatory=$true)][int]$RenderDevice,
    [Parameter(Mandatory=$true)][int]$LoopbackDevice,
    [int]$SampleRate=44100,
    [ValidateRange(30,60)][int]$Seconds=60
)
$ErrorActionPreference='Stop'
$results=[IO.Path]::GetFullPath($ResultsDirectory)
$previous=(Get-ItemProperty HKLM:\SOFTWARE\EqualizerAPO).ConfigPath
$userConfig=Join-Path $previous 'config.txt'
$settings=Join-Path $results 'runtime-settings.json'
$tempConfig=Join-Path $results 'config\config.txt'
if(!(Test-Path -LiteralPath $tempConfig)){throw 'Prepare the temporary config and *-case.json files first'}
if(Get-Process Editor -ErrorAction SilentlyContinue){throw 'Save and close the existing Editor before starting the installed-device test'}
@{results=$results;previous_config_path=$previous;user_config=$userConfig;
  user_config_sha256=(Get-FileHash -LiteralPath $userConfig).Hash;
  test_config_directory=(Split-Path -Parent $tempConfig);installed_root=$InstalledRoot} |
  ConvertTo-Json | Set-Content -LiteralPath $settings -Encoding UTF8
$helper=$null;$failures=0
try {
    $arguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $PSScriptRoot 'prepare-installed-runtime.ps1')+'" -Settings "'+$settings+'" -ParentPID '+$PID+' -RestartAudio'
    $helper=Start-Process powershell.exe -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -PassThru
    $deadline=[DateTime]::UtcNow.AddSeconds(60)
    while(!(Test-Path -LiteralPath (Join-Path $results 'runtime-ready.json'))){
        if(Test-Path -LiteralPath (Join-Path $results 'runtime-error.json')){throw (Get-Content (Join-Path $results 'runtime-error.json') -Raw)}
        if($helper.HasExited -or [DateTime]::UtcNow -gt $deadline){throw 'Elevated runtime preparation did not finish'}
        Start-Sleep -Milliseconds 250
    }
    foreach($case in (Get-ChildItem -LiteralPath $results -Filter '*-case.json' | Sort-Object Name)){
        & python (Join-Path $PSScriptRoot 'live_panel_regression.py') --editor (Join-Path $InstalledRoot 'Editor.exe') --config $tempConfig --case-file $case.FullName --output (Join-Path $results $case.BaseName) --render-device $RenderDevice --loopback-device $LoopbackDevice --sample-rate $SampleRate --seconds $Seconds
        if($LASTEXITCODE -ne 0){$failures++}
    }
} finally {
    New-Item -ItemType File -Path (Join-Path $results 'runtime-stop') -Force | Out-Null
    if($helper -and !$helper.HasExited){$helper.WaitForExit(30000) | Out-Null}
    if(!(Test-Path -LiteralPath (Join-Path $results 'runtime-restored.json'))){throw 'Runtime restoration receipt missing; inspect ConfigPath before continuing'}
    $restored=Get-Content (Join-Path $results 'runtime-restored.json') -Raw | ConvertFrom-Json
    if(!$restored.config_path_restored -or !$restored.user_config_unchanged){throw 'Configuration restoration verification failed'}
}
Write-Output "Installed-device cases completed; failures=$failures; results=$results"
if($failures){exit 1}
