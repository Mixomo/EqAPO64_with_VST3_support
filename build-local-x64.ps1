param(
	[string]$Configuration = "Release",
	[string]$PlatformToolset = "v143",
	[string]$VisualStudioEdition = "Community"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$thirdParty = Join-Path $root "third_party"
$triplet = "x64-windows"

$paths = @{
	LIBSNDFILE_INCLUDE = Join-Path $thirdParty "vcpkg_installed\$triplet\include"
	LIBSNDFILE_LIB = Join-Path $thirdParty "vcpkg_installed\$triplet\lib"
	FFTW_INCLUDE = Join-Path $thirdParty "vcpkg_installed\$triplet\include"
	FFTW_LIB = Join-Path $thirdParty "vcpkg_installed\$triplet\lib"
	MUPARSERX_INCLUDE = Join-Path $thirdParty "muparserx\parser"
	MUPARSERX_LIB = Join-Path $thirdParty "build\muparserx-$triplet\$Configuration"
	TCLAP_ROOT = Join-Path $thirdParty "tclap"
}

foreach ($path in $paths.GetEnumerator()) {
	if (!(Test-Path $path.Value)) {
		throw "Missing dependency path $($path.Key): $($path.Value)"
	}
}

$vsDevCmd = Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022\$VisualStudioEdition\Common7\Tools\VsDevCmd.bat"
if (!(Test-Path $vsDevCmd)) {
	throw "Visual Studio 2022 $VisualStudioEdition VsDevCmd.bat not found: $vsDevCmd"
}

$windowsKitInclude = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include"
$windowsSdkVersion = ""
if (Test-Path -LiteralPath $windowsKitInclude) {
	$windowsSdkVersion = Get-ChildItem -LiteralPath $windowsKitInclude -Directory |
		Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "um\Windows.h") } |
		Sort-Object Name -Descending |
		Select-Object -First 1 -ExpandProperty Name
}
if ($windowsSdkVersion -eq "") {
	throw "Windows 10 SDK was not found under $windowsKitInclude"
}

# Avoid duplicate PATH/Path process variables confusing MSBuild's CL task.
[Environment]::SetEnvironmentVariable("PATH", $null, "Process")
$systemRoot = $env:SystemRoot
[Environment]::SetEnvironmentVariable("Path", "$systemRoot\System32;$systemRoot;$systemRoot\System32\Wbem", "Process")

$props = @(
	"/p:Configuration=$Configuration",
	"/p:Platform=x64",
	"/p:PlatformToolset=$PlatformToolset",
	"/p:LIBSNDFILE_INCLUDE=$($paths.LIBSNDFILE_INCLUDE)",
	"/p:LIBSNDFILE_LIB=$($paths.LIBSNDFILE_LIB)",
	"/p:FFTW_INCLUDE=$($paths.FFTW_INCLUDE)",
	"/p:FFTW_LIB=$($paths.FFTW_LIB)",
	"/p:MUPARSERX_INCLUDE=$($paths.MUPARSERX_INCLUDE)",
	"/p:MUPARSERX_LIB=$($paths.MUPARSERX_LIB)",
	"/p:TCLAP_ROOT=$($paths.TCLAP_ROOT)",
	"/p:WindowsTargetPlatformVersion=$windowsSdkVersion",
	"/m"
)

$commands = @(
	"msbuild Common.vcxproj $($props -join ' ')",
	"msbuild EqualizerAPO\EqualizerAPO.vcxproj $($props -join ' ')",
	"msbuild EqApoOutProcHost\EqApoOutProcHost.vcxproj $($props -join ' ')",
	"msbuild Benchmark\Benchmark.vcxproj $($props -join ' ')",
	"msbuild VoicemeeterClient\VoicemeeterClient.vcxproj $($props -join ' ')"
)

$cmd = "set WindowsTargetPlatformVersion=$windowsSdkVersion && set WindowsSDKVersion=$windowsSdkVersion\ && call `"$vsDevCmd`" -winsdk=$windowsSdkVersion && " + ($commands -join " && ")
Push-Location $root
try {
	cmd /c $cmd
	if ($LASTEXITCODE -ne 0) {
		exit $LASTEXITCODE
	}
}
finally {
	Pop-Location
}
