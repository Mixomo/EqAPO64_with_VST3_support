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

Require-Command git
Require-Command cmake
Require-Command python

$repos = @(
    @{ Name = "vcpkg"; Url = "https://github.com/microsoft/vcpkg.git"; Path = Join-Path $thirdParty "vcpkg" },
    @{ Name = "VST3 SDK"; Url = "https://github.com/steinbergmedia/vst3sdk.git"; Path = Join-Path $thirdParty "vst3sdk" },
    @{ Name = "muparserx"; Url = "https://github.com/beltoforion/muparserx.git"; Path = Join-Path $thirdParty "muparserx" },
    @{ Name = "TCLAP"; Url = "https://github.com/mirror/tclap.git"; Path = Join-Path $thirdParty "tclap" }
)

foreach ($repo in $repos) {
    if (Test-Path -LiteralPath $repo.Path) {
        Write-Host "$($repo.Name) already exists: $($repo.Path)"
    } else {
        git clone --depth 1 $repo.Url $repo.Path
    }
}

$vcpkgExe = Join-Path $thirdParty "vcpkg\vcpkg.exe"
if (-not (Test-Path -LiteralPath $vcpkgExe)) {
    & (Join-Path $thirdParty "vcpkg\bootstrap-vcpkg.bat") -disableMetrics
}

$vcpkgInstallRoot = Join-Path $thirdParty "vcpkg_installed"
& $vcpkgExe install --triplet $Triplet --x-install-root=$vcpkgInstallRoot
if ($LASTEXITCODE -ne 0) {
    throw "vcpkg install failed with exit code $LASTEXITCODE"
}

$muparserBuild = Join-Path $thirdParty "build\muparserx-$Triplet"
$muparserSource = Join-Path $thirdParty "muparserx"
$muparserLib = Join-Path $muparserBuild "$Configuration\muparserx.lib"
if (Test-Path -LiteralPath $muparserLib) {
    Write-Host "muparserx already built: $muparserLib"
} else {
    cmake -S $muparserSource -B $muparserBuild -A x64 -DUSE_WIDE_STRING=ON -DCMAKE_BUILD_TYPE=$Configuration
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
