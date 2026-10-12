param(
	[string]$BaseUrl = "https://raw.githubusercontent.com/IJustItay/Neon-Equalizer/main/public/targets",
	[string]$OutputDir = (Join-Path (Split-Path $PSScriptRoot -Parent) "resources\HeadphoneCalibrations")
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$indexUrl = "$BaseUrl/index.json"
$indexPath = Join-Path $OutputDir "index.json"
Invoke-WebRequest -Uri $indexUrl -OutFile $indexPath

$index = Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json
foreach ($target in $index.targets) {
	$fileName = [string]$target.file
	if ([string]::IsNullOrWhiteSpace($fileName)) { continue }
	$escaped = [uri]::EscapeDataString($fileName).Replace("%2F", "/")
	Invoke-WebRequest -Uri "$BaseUrl/$escaped" -OutFile (Join-Path $OutputDir $fileName)
}

$count = (Get-ChildItem -LiteralPath $OutputDir -File | Measure-Object).Count
Write-Host "Updated headphone calibration bundle: $count files in $OutputDir"
