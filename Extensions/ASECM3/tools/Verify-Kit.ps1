param([string]$KitRoot=$PSScriptRoot)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($KitRoot).TrimEnd('\','/')
if(Test-Path -LiteralPath (Join-Path $root '.asecm3-export-incomplete')){throw 'Kit export is incomplete'}
$manifest=Join-Path $root 'SHA256SUMS.txt'
if(!(Test-Path -LiteralPath $manifest -PathType Leaf)){throw 'Kit checksum manifest missing'}
$seen=@{};$count=0
foreach($line in (Get-Content -LiteralPath $manifest)){
    if($line -notmatch '^([A-Fa-f0-9]{64})  (.+)$'){throw 'Invalid kit checksum line'}
    $expected=$Matches[1];$relative=$Matches[2]
    if([IO.Path]::IsPathRooted($relative) -or ($relative -split '[/\\]' | Where-Object {$_ -eq '..'})){throw 'Invalid kit-relative checksum path'}
    $path=[IO.Path]::GetFullPath((Join-Path $root $relative))
    if(!$path.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Kit checksum path escaped root'}
    if($seen.ContainsKey($path)){throw 'Duplicate kit checksum path'};$seen[$path]=$true
    $item=Get-Item -LiteralPath $path
    if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Kit input is a reparse point: $relative"}
    $ancestor=Split-Path $path
    while($ancestor){
        if((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Kit path traverses a reparse point: $relative"}
        if($ancestor -eq $root){break};$ancestor=Split-Path $ancestor
    }
    if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $expected){throw "Kit checksum mismatch: $relative"}
    ++$count
}
Write-Output "Verified $count kit input files."
