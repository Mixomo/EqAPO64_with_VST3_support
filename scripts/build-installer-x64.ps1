param(
    [ValidateSet('Release')][string]$Configuration='Release',
    [string]$VisualStudioEdition='Community',
    [string]$Makensis='',
    [switch]$Rebuild
)
$root = Split-Path -Parent $PSScriptRoot
$arguments = @('-Configuration',$Configuration,'-VisualStudioEdition',$VisualStudioEdition)
if ($Makensis) { $arguments += @('-Makensis',$Makensis) }
if ($Rebuild) { $arguments += '-Rebuild' }
& (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'build.ps1') @arguments
exit $LASTEXITCODE
