param(
    [Parameter(Mandatory=$true)][string]$Settings,
    [Parameter(Mandatory=$true)][int]$ParentPID,
    [switch]$RestartAudio
)
# Explicit elevated test-config redirection. Never installs or closes user apps.
$ErrorActionPreference='Stop'
$state=Get-Content -LiteralPath $Settings -Raw | ConvertFrom-Json
$results=[IO.Path]::GetFullPath($state.results)
$sourceRoot=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if(!$results.StartsWith((Join-Path $sourceRoot 'output\live-audio\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected results directory'}
$registry=$null;$redirected=$false
try {
    if(!(Get-Process -Id $ParentPID -ErrorAction SilentlyContinue)){throw 'Test owner exited'}
    $registry=[Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SOFTWARE\EqualizerAPO',$true)
    if($registry.GetValue('ConfigPath') -ne $state.previous_config_path){throw 'ConfigPath changed before test'}
    if((Get-FileHash -LiteralPath $state.user_config).Hash -ne $state.user_config_sha256){throw 'User configuration changed before test; preserve it'}
    $registry.SetValue('ConfigPath',$state.test_config_directory,[Microsoft.Win32.RegistryValueKind]::String)
    $redirected=$true
    if($RestartAudio){Restart-Service -Name Audiosrv -Force}
    $hashes=@{}
    foreach($name in @('Editor.exe','EqualizerAPO.dll','EqApoOutProcHost.exe')){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $state.installed_root $name)).Hash}
    @{installed=$true;config_path=$registry.GetValue('ConfigPath');hashes=$hashes} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $results 'runtime-ready.json') -Encoding UTF8
    $deadline=[DateTime]::UtcNow.AddMinutes(25)
    while([DateTime]::UtcNow -lt $deadline -and !(Test-Path -LiteralPath (Join-Path $results 'runtime-stop'))){
        if(!(Get-Process -Id $ParentPID -ErrorAction SilentlyContinue)){break}
        Start-Sleep -Milliseconds 250
    }
} catch {
    @{error=$_.Exception.Message} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $results 'runtime-error.json') -Encoding UTF8
} finally {
    if($registry -and $redirected -and $registry.GetValue('ConfigPath') -eq $state.test_config_directory){
        $registry.SetValue('ConfigPath',$state.previous_config_path,[Microsoft.Win32.RegistryValueKind]::String)
        if($RestartAudio){Restart-Service -Name Audiosrv -Force}
    }
    @{config_path_restored=(!$registry -or $registry.GetValue('ConfigPath') -eq $state.previous_config_path);user_config_unchanged=((Get-FileHash -LiteralPath $state.user_config).Hash -eq $state.user_config_sha256)} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $results 'runtime-restored.json') -Encoding UTF8
    if($registry){$registry.Dispose()}
}
