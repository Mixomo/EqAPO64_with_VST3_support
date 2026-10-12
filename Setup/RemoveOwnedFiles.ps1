param([Parameter(Mandatory=$true)][string]$Root,
      [Parameter(Mandatory=$true)][string]$ManifestPath,
      [Parameter(Mandatory=$true)][string]$OwnershipPath,
      [ValidateSet('Prepare','Remove')][string]$Mode='Remove')
$ErrorActionPreference='Stop'
$base=[IO.Path]::GetFullPath($Root).TrimEnd('\')
if ((Split-Path $base -Leaf) -notin @('IRs','HeadphoneCalibrations')) {throw 'Unexpected payload root'}
$ownership=[IO.Path]::GetFullPath($OwnershipPath)
if ((Split-Path $ownership -Parent) -ne (Split-Path $base -Parent) -or (Split-Path $ownership -Leaf) -notlike '.eqapo-owned-*.json') {throw 'Unexpected ownership path'}
function Test-SafePath([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    if (!$full.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase) -and $full -ne $base) {return $false}
    $current=$full
    while ($current.Length -ge $base.Length) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {return $false}
        }
        if ($current -eq $base) {break}
        $current=Split-Path $current -Parent
    }
    return $true
}
$directories=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$previous=@()
if(Test-Path -LiteralPath $ownership -PathType Leaf) {
    try {$previous=Get-Content -LiteralPath $ownership -Raw | ConvertFrom-Json}
    catch {Write-Output 'Unusable ownership record: preserving existing files'}
}
if($Mode -eq 'Prepare') {
    $next=@()
    foreach($entry in (Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json)) {
        $path=Join-Path $base $entry.path
        if(!(Test-SafePath $path)) {continue}
        if(!(Test-Path -LiteralPath $path)) {$next+=@{path=$entry.path;sha256=$entry.sha256};continue}
        $prior=$previous | Where-Object path -eq $entry.path | Select-Object -First 1
        if($prior -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $prior.sha256) {$next+=@{path=$prior.path;sha256=$prior.sha256}}
    }
    ConvertTo-Json -InputObject @($next) -Depth 4 | Set-Content -LiteralPath ($ownership+'.tmp') -Encoding UTF8
    Move-Item -LiteralPath ($ownership+'.tmp') -Destination $ownership -Force
    exit 0
}
foreach ($entry in $previous) {
    $path=Join-Path $base $entry.path
    if (!(Test-SafePath $path)) {Write-Output "Preserved unsafe/reparse path: $($entry.path)";continue}
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $entry.sha256) {
            Remove-Item -LiteralPath $path -Force
        } else {Write-Output "Preserved modified file: $($entry.path)"}
    }
    $parent=Split-Path $path -Parent
    while ($parent.Length -ge $base.Length) {
        [void]$directories.Add($parent)
        if ($parent -eq $base) {break}
        $parent=Split-Path $parent -Parent
    }
}
foreach ($directory in ($directories | Sort-Object Length -Descending)) {
    if ((Test-SafePath $directory) -and (Test-Path -LiteralPath $directory -PathType Container)) {
        if (!(Get-ChildItem -LiteralPath $directory -Force | Select-Object -First 1)) {Remove-Item -LiteralPath $directory}
    }
}
if(Test-Path -LiteralPath $ownership -PathType Leaf) {Remove-Item -LiteralPath $ownership}
