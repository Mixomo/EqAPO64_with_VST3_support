param(
	[string]$Configuration = "Release",
	[string]$VisualStudioEdition = "Community",
	[string]$Makensis = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

& (Join-Path $root "scripts\bootstrap-third-party.ps1") -Configuration $Configuration -WithQt -WithNsis
& (Join-Path $root "build-local-x64.ps1") -Configuration $Configuration -VisualStudioEdition $VisualStudioEdition
if ($LASTEXITCODE -ne 0) {
	throw "native build failed with exit code $LASTEXITCODE"
}
& (Join-Path $root "scripts\build-qt-apps-x64.ps1") -Configuration $Configuration -VisualStudioEdition $VisualStudioEdition
if ($LASTEXITCODE -ne 0) {
	throw "Qt app build failed with exit code $LASTEXITCODE"
}
& (Join-Path $root "scripts\stage-installer-x64.ps1") -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
	throw "staging failed with exit code $LASTEXITCODE"
}

if ($Makensis -eq "") {
	$localMakensis = Join-Path $root "third_party\nsis-3.11\makensis.exe"
	if (Test-Path -LiteralPath $localMakensis) {
		$Makensis = $localMakensis
	}
}

if ($Makensis -eq "") {
	$makensisCommand = Get-Command makensis.exe -ErrorAction SilentlyContinue
	if ($null -ne $makensisCommand) {
		$Makensis = $makensisCommand.Source
	}
}

if ($Makensis -eq "") {
	$candidates = @(
		Join-Path ${env:ProgramFiles} "NSIS\makensis.exe",
		Join-Path ${env:ProgramFiles(x86)} "NSIS\makensis.exe"
	)
	$found = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
	if ($null -ne $found) {
		$Makensis = $found
	}
}

if ($Makensis -eq "") {
	throw "makensis.exe was not found. Install NSIS or pass -Makensis <path>."
}

Push-Location (Join-Path $root "Setup")
try {
	& $Makensis ".\Setup64.nsi"
	if ($LASTEXITCODE -ne 0) {
		throw "makensis failed with exit code $LASTEXITCODE"
	}
}
finally {
	Pop-Location
}

Write-Host "Installer build finished."
