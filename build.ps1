param(
    [ValidateSet('Release')][string]$Configuration = 'Release',
    [ValidateSet('Community','Professional','Enterprise','BuildTools')][string]$VisualStudioEdition = 'Community',
    [string]$Makensis = '',
    [switch]$Rebuild
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$shell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$python = (Get-Command python.exe -ErrorAction Stop).Source
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logs = Join-Path $root "output\build\$stamp"
New-Item -ItemType Directory -Force -Path $logs | Out-Null
function Stage([string]$Name, [string]$Script, [string[]]$Arguments = @()) {
    Write-Host "[$Name]"
    # Each child has its own environment: the MSVC PATH reset cannot affect Qt/Python.
    & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root $Script) @Arguments 2>&1 |
        Tee-Object -FilePath (Join-Path $logs "$Name.log")
    if ($LASTEXITCODE -ne 0) { throw "$Name failed: exit $LASTEXITCODE. Logs: $logs" }
}
try {
    Stage '01-dependencies' 'scripts\bootstrap-third-party.ps1' @('-Configuration',$Configuration,'-WithQt','-WithNsis')
    $nativeArgs = @('-Configuration',$Configuration,'-VisualStudioEdition',$VisualStudioEdition)
    if ($Rebuild) { $nativeArgs += '-Rebuild' }
    Stage '02-native' 'build-local-x64.ps1' $nativeArgs
    Stage '03-editor' 'scripts\build-qt-apps-x64.ps1' $nativeArgs
    Stage '04-payload' 'scripts\stage-installer-x64.ps1' @('-Configuration',$Configuration)
    & $python (Join-Path $root 'scripts\generate-wide-installer-ui.py') 2>&1 |
        Tee-Object -FilePath (Join-Path $logs '05-installer-ui.log')
    if ($LASTEXITCODE -ne 0) { throw 'Installer UI generation failed' }
    if (!$Makensis) { $Makensis = Join-Path $root 'third_party\nsis-3.11\makensis.exe' }
    if (!(Test-Path -LiteralPath $Makensis)) { throw "NSIS not found: $Makensis" }
    $temporary = Join-Path $root "_build\installer\$stamp"
    New-Item -ItemType Directory -Force -Path $temporary | Out-Null
    $name = 'EqualizerAPO-x64-1.4.2-with-VST3-support-V3.0.0.exe'
    $candidate = Join-Path $temporary $name
    Push-Location (Join-Path $root 'Setup')
    try {
        & $Makensis "/DOUTPUT_FILE=$candidate" 'Setup64.nsi' 2>&1 |
            Tee-Object -FilePath (Join-Path $logs '06-nsis.log')
        if ($LASTEXITCODE -ne 0) { throw "NSIS failed: $LASTEXITCODE" }
    } finally { Pop-Location }
    if (!(Test-Path -LiteralPath $candidate)) { throw 'NSIS did not produce the installer' }
    $output = Join-Path $root 'output\installer'
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $installer = Join-Path $output $name
    Copy-Item -LiteralPath $candidate -Destination $installer -Force
    $artifacts = @($installer)
    foreach ($file in @('Editor.exe','EqualizerAPO.dll','EqApoOutProcHost.exe')) {
        $artifacts += Join-Path $root "x64\$Configuration\$file"
    }
    $hashes = @($artifacts | ForEach-Object {
        @{file=$_.Substring($root.Length+1); sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    @{configuration=$Configuration; artifacts=$hashes; testsExecuted=$false; installed=$false} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $logs 'build-result.json') -Encoding UTF8
    Write-Host "Instalador: $installer"
    Write-Host "Logs: $logs"
    exit 0
} catch { Write-Error $_; exit 1 }
