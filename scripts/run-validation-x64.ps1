param(
    [ValidateRange(1,60)][int]$SmokeSeconds=60,
    [ValidateSet('Core','Transport','All')][string]$Suite='All',
    [string]$MatrixPath='',
    [switch]$WithFixtureGui
)
$root=Split-Path -Parent $PSScriptRoot
$arguments=@('-SmokeSeconds',"$SmokeSeconds",'-Suite',$Suite)
if ($MatrixPath) { $arguments+=@('-MatrixPath',$MatrixPath) }
if ($WithFixtureGui) { $arguments+='-WithFixtureGui' }
& (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'SmokeTests\run-smoke.ps1') @arguments
exit $LASTEXITCODE
