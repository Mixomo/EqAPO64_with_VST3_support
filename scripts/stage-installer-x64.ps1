param(
	[string]$Configuration = "Release",
	[string]$QtRoot = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$thirdParty = Join-Path $root "third_party"
$triplet = "x64-windows"
$outDir = Join-Path $root "x64\$Configuration"
$libDir = Join-Path $root "Setup\lib64"

if (!(Test-Path -LiteralPath $outDir)) {
	throw "Build output directory not found: $outDir"
}

if ($QtRoot -eq "") {
	$qtCandidates = Get-ChildItem -Path (Join-Path $thirdParty "Qt") -Filter windeployqt.exe -Recurse -ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -match "\\msvc2022_64\\bin\\windeployqt.exe$" } |
		Sort-Object FullName
	if ($qtCandidates.Count -eq 0) {
		throw "Qt windeployqt.exe not found under third_party\Qt. Run scripts\bootstrap-third-party.ps1 -WithQt first."
	}
	$QtRoot = Split-Path -Parent (Split-Path -Parent $qtCandidates[0].FullName)
}

$windeployqt = Join-Path $QtRoot "bin\windeployqt.exe"
if (!(Test-Path -LiteralPath $windeployqt)) {
	throw "windeployqt.exe not found: $windeployqt"
}

$requiredBinaries = @{
	(Join-Path $root "EqualizerAPO\x64\$Configuration\EqualizerAPO.dll") = "EqualizerAPO.dll"
	(Join-Path $root "EqApoOutProcHost\x64\$Configuration\EqApoOutProcHost.exe") = "EqApoOutProcHost.exe"
	(Join-Path $root "Benchmark\x64\$Configuration\Benchmark.exe") = "Benchmark.exe"
	(Join-Path $root "VoicemeeterClient\x64\$Configuration\VoicemeeterClient.exe") = "VoicemeeterClient.exe"
}

foreach ($entry in $requiredBinaries.GetEnumerator()) {
	if (!(Test-Path -LiteralPath $entry.Key)) {
		throw "Required binary not found: $($entry.Key)"
	}
	Copy-Item -LiteralPath $entry.Key -Destination (Join-Path $outDir $entry.Value) -Force
}

$qtApps = @("Editor.exe", "DeviceSelector.exe", "UpdateChecker.exe")
foreach ($app in $qtApps) {
	$appPath = Join-Path $outDir $app
	if (!(Test-Path -LiteralPath $appPath)) {
		throw "Required Qt app not found: $appPath"
	}
}

Remove-Item -LiteralPath $libDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $libDir | Out-Null

$vcpkgBin = Join-Path $thirdParty "vcpkg_installed\$triplet\bin"
$runtimeDlls = @(
	"fftw3.dll",
	"sndfile.dll",
	"FLAC.dll",
	"libmp3lame.dll",
	"mpg123.dll",
	"ogg.dll",
	"opus.dll",
	"vorbis.dll",
	"vorbisenc.dll",
	"vorbisfile.dll"
)

foreach ($dll in $runtimeDlls) {
	$src = Join-Path $vcpkgBin $dll
	if (!(Test-Path -LiteralPath $src)) {
		throw "Runtime DLL not found: $src"
	}
	Copy-Item -LiteralPath $src -Destination (Join-Path $libDir $dll) -Force
}

Copy-Item -LiteralPath (Join-Path $vcpkgBin "fftw3.dll") -Destination (Join-Path $libDir "libfftw3.dll") -Force

$vcRuntimeDlls = @(
	"msvcp140.dll",
	"msvcp140_1.dll",
	"vcruntime140.dll",
	"vcruntime140_1.dll"
)

$vcRedistCandidates = Get-ChildItem -Path @(
	(Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022"),
	(Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022")
) -Recurse -Directory -ErrorAction SilentlyContinue |
	Where-Object { $_.FullName -match "\\VC\\Redist\\MSVC\\[^\\]+\\x64\\Microsoft\.VC\d+\.CRT$" } |
	Sort-Object FullName -Descending

$vcRedistDir = $null
foreach ($candidate in $vcRedistCandidates) {
	$missing = $false
	foreach ($dll in $vcRuntimeDlls) {
		if (!(Test-Path -LiteralPath (Join-Path $candidate.FullName $dll))) {
			$missing = $true
			break
		}
	}
	if (!$missing) {
		$vcRedistDir = $candidate.FullName
		break
	}
}

if ($null -eq $vcRedistDir) {
	throw "Visual C++ x64 runtime redistributable files were not found. Install Visual Studio 2022 C++ redistributables or build tools."
}

foreach ($dll in $vcRuntimeDlls) {
	Copy-Item -LiteralPath (Join-Path $vcRedistDir $dll) -Destination (Join-Path $libDir $dll) -Force
}

$deployDir = Join-Path $root "_build\qt-deploy-x64"
Remove-Item -LiteralPath $deployDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $deployDir | Out-Null

foreach ($app in $qtApps) {
	$tempApp = Join-Path $deployDir $app
	Copy-Item -LiteralPath (Join-Path $outDir $app) -Destination $tempApp -Force
	& $windeployqt --release --no-translations --no-compiler-runtime --dir $deployDir $tempApp
	if ($LASTEXITCODE -ne 0) {
		throw "windeployqt failed for $app with exit code $LASTEXITCODE"
	}
}

$qtDlls = @("d3dcompiler_47.dll", "icuuc.dll", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Network.dll", "Qt6Svg.dll", "Qt6Widgets.dll")
foreach ($dll in $qtDlls) {
	$src = Join-Path $deployDir $dll
	if (!(Test-Path -LiteralPath $src)) {
		throw "Qt deployed DLL not found: $src"
	}
	Copy-Item -LiteralPath $src -Destination (Join-Path $libDir $dll) -Force
}

$optionalQtDlls = @("dxcompiler.dll", "dxil.dll")
foreach ($dll in $optionalQtDlls) {
	$src = Join-Path $deployDir $dll
	if (Test-Path -LiteralPath $src) {
		Copy-Item -LiteralPath $src -Destination (Join-Path $libDir $dll) -Force
	}
}

$pluginFiles = @(
	"generic\qtuiotouchplugin.dll",
	"iconengines\qsvgicon.dll",
	"imageformats\qgif.dll",
	"imageformats\qico.dll",
	"imageformats\qjpeg.dll",
	"imageformats\qsvg.dll",
	"networkinformation\qnetworklistmanager.dll",
	"platforms\qwindows.dll",
	"styles\qmodernwindowsstyle.dll",
	"tls\qcertonlybackend.dll",
	"tls\qschannelbackend.dll"
)

foreach ($plugin in $pluginFiles) {
	$src = Join-Path $deployDir $plugin
	if (!(Test-Path -LiteralPath $src)) {
		throw "Qt plugin not found after deploy: $src"
	}
	$dest = Join-Path $libDir ("qt\" + $plugin)
	New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
	Copy-Item -LiteralPath $src -Destination $dest -Force
}

$headphoneCalSrc = Join-Path $root "resources\HeadphoneCalibrations"
if (!(Test-Path -LiteralPath (Join-Path $headphoneCalSrc "ash_hpcf_catalog.json"))) {
	throw "Bundled headphone calibration catalog not found: $headphoneCalSrc"
}
$headphoneCalDest = Join-Path $libDir "HeadphoneCalibrations"
New-Item -ItemType Directory -Force -Path $headphoneCalDest | Out-Null
Copy-Item -LiteralPath (Join-Path $headphoneCalSrc "ash_hpcf_catalog.json") -Destination $headphoneCalDest -Force

$irSrc = Join-Path $root "IRs"
if (Test-Path -LiteralPath $irSrc) {
	$irDest = Join-Path $libDir "IRs"
	New-Item -ItemType Directory -Force -Path $irDest | Out-Null
	Copy-Item -Path (Join-Path $irSrc "*") -Destination $irDest -Recurse -Force
}

Write-Host "Installer staging is ready: $outDir and $libDir."
