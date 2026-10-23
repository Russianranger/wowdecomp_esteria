param(
    [Parameter(Mandatory=$true)][string]$Destination,
    [switch]$IncludeRuntime,
    [switch]$IncludeLibraries,
    [string]$StageRoot,
    [string]$HostBuildRoot
)
$ErrorActionPreference='Stop'
$module=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\','/')
$repo=[IO.Path]::GetFullPath((Join-Path $module '../..')).TrimEnd('\','/')
$root=[IO.Path]::GetFullPath($Destination).TrimEnd('\','/')
if(Test-Path -LiteralPath $root){throw 'Export destination already exists; choose a new folder'}
if($root.StartsWith($module+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Export destination must be outside the module source tree'}
if(!$StageRoot){$StageRoot=Join-Path $repo '../deploy/ASEC335'}
if(!$HostBuildRoot){$HostBuildRoot=Join-Path $repo 'build/x64-ninja'}
New-Item -ItemType Directory -Path $root|Out-Null
$pending=Join-Path $root '.asecm3-export-incomplete'
'Export in progress'|Set-Content -LiteralPath $pending
function Copy-Input([string]$Source,[string]$Relative){
    if([IO.Path]::IsPathRooted($Relative) -or ($Relative -split '[/\\]'|Where-Object {$_ -eq '..'})){throw 'Invalid export path'}
    $target=[IO.Path]::GetFullPath((Join-Path $root $Relative))
    if(!$target.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Export path escaped destination'}
    if((Get-Item -LiteralPath $Source).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Source input is a reparse point: $Source"}
    New-Item -ItemType Directory -Force -Path (Split-Path $target)|Out-Null
    Copy-Item -LiteralPath $Source -Destination $target
    if((Get-FileHash -LiteralPath $Source).Hash -ine (Get-FileHash -LiteralPath $target).Hash){throw "Export readback mismatch: $Relative"}
}
foreach($item in(Get-ChildItem -LiteralPath $module -Recurse -File)){
    $relative=$item.FullName.Substring($module.Length+1).Replace('\','/')
    if($relative -match '(^|/)(__pycache__|build)(/|$)' -or $relative -match '\.py[co]$'){continue}
    Copy-Input $item.FullName ('Modules/ASECM3/'+$relative)
}
Copy-Input (Join-Path $PSScriptRoot 'Verify-Kit.ps1') 'Verify-Kit.ps1'
Copy-Input (Join-Path $PSScriptRoot 'Build-Core.ps1') 'Build-Core.ps1'
Copy-Input (Join-Path $module 'docs/PORTING.md') 'README.md'
$manifestPath=Join-Path $repo '../verification/m3/module-manifest.json'
$hostManifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
$reference=@()
foreach($entry in $hostManifest.integration){
    $path=Join-Path $repo $entry.path;$hits=@();$lines=Get-Content -LiteralPath $path
    for($i=0;$i -lt $lines.Count;++$i){if($lines[$i] -match 'ASECM3|ASEC_ASSET_M3'){
        $first=[Math]::Max(0,$i-3);$last=[Math]::Min($lines.Count-1,$i+3)
        $hits+=[ordered]@{line=$i+1;context=@($lines[$first..$last])}
    }}
    $reference+=[ordered]@{path=$entry.path;sha256=(Get-FileHash -LiteralPath $path).Hash;sites=$hits}
}
$reference|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root 'host-hook-reference.json')
$revision=& git -C $repo rev-parse HEAD
if($LASTEXITCODE){throw 'Cannot identify host source revision'}
$identity=[ordered]@{exportedAt=(Get-Date).ToString('o');hostBaseCommit=$revision;hostWorkingTree=$true;runtimeIncluded=[bool]$IncludeRuntime;librariesIncluded=[bool]$IncludeLibraries;architecture='x64';compilerRuntime='MSVC v143 Release /MD; rebuild when host/compiler ABI differs';scope='Portable source and integration reference; no automatic arbitrary-host patch or runtime transfer acceptance'}
if($IncludeLibraries){
    foreach($name in @('ASECM3Core.lib','ASECM3Client.lib','ASECM3Bullet.lib')){Copy-Input (Join-Path $HostBuildRoot ('lib/'+$name)) ('Libraries/x64/'+$name)}
}
if($IncludeRuntime){
    $runtimeManifest=Join-Path $StageRoot 'ASEC-M3-SHA256.txt'
    if(!(Get-Content -LiteralPath $runtimeManifest|Where-Object {$_ -match '^[A-Fa-f0-9]{64}  XvidDecoder\.dll$'})){throw 'Runtime manifest lacks the required XvidDecoder.dll'}
    foreach($line in(Get-Content -LiteralPath $runtimeManifest)){
        if($line -notmatch '^([A-Fa-f0-9]{64})  (.+)$'){throw 'Invalid runtime manifest'}
        $expected=$Matches[1];$relative=$Matches[2];$source=Join-Path $StageRoot $relative
        if((Get-FileHash -LiteralPath $source).Hash -ine $expected){throw "Runtime stage mismatch: $relative"}
        Copy-Input $source ('Runtime/'+$relative.Replace('\','/'))
    }
    Copy-Input $runtimeManifest 'Runtime/ASEC-M3-SHA256.txt'
    Copy-Input (Join-Path $StageRoot 'Install-ASECM3.ps1') 'Runtime/Install-ASECM3.ps1'
    $identity.runtimeExeSHA256=(Get-FileHash -LiteralPath (Join-Path $StageRoot 'WowClient64-ASEC.exe')).Hash
}
$identity|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $root 'source-identity.json')
$checks=@(foreach($item in(Get-ChildItem -LiteralPath $root -Recurse -File|Sort-Object FullName)){
    if($item.FullName -eq $pending){continue}
    (Get-FileHash -LiteralPath $item.FullName).Hash.ToLowerInvariant()+'  '+$item.FullName.Substring($root.Length+1).Replace('\','/')
})
$checks|Set-Content -LiteralPath (Join-Path $root 'SHA256SUMS.txt')
Remove-Item -LiteralPath $pending
& (Join-Path $root 'Verify-Kit.ps1') -KitRoot $root
Write-Output "Exported ASECM3 kit to $root"
