param(
    [string] $Triplet = "x64-windows",
    [string] $Configuration = "Release",
    [switch] $WithQt,
    [string] $QtVersion = "6.10.1",
    [string] $QtArch = "win64_msvc2022_64",
    [switch] $WithNsis,
    [string] $NsisVersion = "3.11"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$thirdParty = Join-Path $root "third_party"
New-Item -ItemType Directory -Force -Path $thirdParty | Out-Null

function Require-Command($Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name was not found in PATH"
    }
}


$sdkHeader = Join-Path $thirdParty 'vst3sdk\pluginterfaces\vst\ivstaudioprocessor.h'
if (!(Test-Path -LiteralPath $sdkHeader)) { throw 'Missing bundled VST3 SDK. Restore third_party/vst3sdk from this source package.' }
foreach ($source in @('muparserx\parser','tclap\include')) {
    if (!(Test-Path -LiteralPath (Join-Path $thirdParty $source))) { throw "Missing bundled dependency source: $source" }
}
$vcpkgInstallRoot = Join-Path $thirdParty 'vcpkg_installed'
$dependencies = @('include\sndfile.h','include\fftw3.h','lib\sndfile.lib','lib\fftw3.lib','bin\sndfile.dll','bin\fftw3.dll')
$missing = @($dependencies | Where-Object { !(Test-Path -LiteralPath (Join-Path $vcpkgInstallRoot "$Triplet\$_")) })
if ($missing.Count) {
    throw "Bundled dependency cache incomplete: $($missing -join ', '). Restore third_party/vcpkg_installed. No automatic SDK upgrade/download is performed."
}
Write-Host 'Using bundled dependency sources and x64 cache.'

$muparserBuild = Join-Path $thirdParty "build\muparserx-$Triplet"
$muparserSource = Join-Path $thirdParty "muparserx"
$muparserLib = Join-Path $muparserBuild "$Configuration\muparserx.lib"
if (Test-Path -LiteralPath $muparserLib) {
    Write-Host "muparserx already built: $muparserLib"
} else {
    Require-Command cmake
    cmake -S $muparserSource -B $muparserBuild -A x64 -DUSE_WIDE_STRING=ON -DBUILD_EXAMPLES=OFF
    if ($LASTEXITCODE -ne 0) {
        throw "muparserx configure failed with exit code $LASTEXITCODE"
    }
    cmake --build $muparserBuild --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "muparserx build failed with exit code $LASTEXITCODE"
    }
}

if ($WithQt) {
    $qtRoot = Join-Path $thirdParty "Qt"
    $qtHost = Join-Path $qtRoot "$QtVersion\msvc2022_64"
    $qmake = Join-Path $qtHost "bin\qmake.exe"
    $windeployqt = Join-Path $qtHost "bin\windeployqt.exe"

    if ((Test-Path -LiteralPath $qmake) -and (Test-Path -LiteralPath $windeployqt)) {
        Write-Host "Qt already exists: $qtHost"
    } else {
        $pythonTools = Join-Path $thirdParty "python"
        New-Item -ItemType Directory -Force -Path $pythonTools | Out-Null

        Require-Command python
        python -m pip install --upgrade --target $pythonTools aqtinstall
        if ($LASTEXITCODE -ne 0) {
            throw "aqtinstall install failed with exit code $LASTEXITCODE"
        }

        $env:PYTHONPATH = $pythonTools
        python -m aqt install-qt windows desktop $QtVersion $QtArch -O $qtRoot --archives qtbase qtsvg
        if ($LASTEXITCODE -ne 0) {
            throw "Qt install failed with exit code $LASTEXITCODE"
        }
    }
}

if ($WithNsis) {
    $nsisRoot = Join-Path $thirdParty "nsis-$NsisVersion"
    $makensis = Join-Path $nsisRoot "makensis.exe"
    if (Test-Path -LiteralPath $makensis) {
        Write-Host "NSIS already exists: $nsisRoot"
    } else {
        Require-Command curl.exe
        $zip = Join-Path $thirdParty "nsis-$NsisVersion.zip"
        curl.exe -L --fail --output $zip "https://sourceforge.net/projects/nsis/files/NSIS%203/$NsisVersion/nsis-$NsisVersion.zip/download"
        if ($LASTEXITCODE -ne 0) {
            throw "NSIS download failed with exit code $LASTEXITCODE"
        }

        Expand-Archive -LiteralPath $zip -DestinationPath $thirdParty -Force
        if (!(Test-Path -LiteralPath $makensis)) {
            throw "NSIS was not extracted to the expected path: $makensis"
        }
    }
}

Write-Host "third_party is ready."
