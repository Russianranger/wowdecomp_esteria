param(
    [Parameter(Mandatory=$true)][string]$ClientRoot,
    [string]$StageRoot=$PSScriptRoot,
    [switch]$Restart,
    [switch]$Probe
)
$ErrorActionPreference='Stop'
function Assert-X64Image([string]$Path){
    $stream=[IO.File]::OpenRead($Path)
    try{
        $reader=[IO.BinaryReader]::new($stream)
        if($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D){throw "Invalid PE image: $Path"}
        $stream.Position=0x3C;$offset=$reader.ReadUInt32()
        if($offset -gt $stream.Length-26){throw "Invalid PE header offset: $Path"}
        $stream.Position=$offset
        if($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664){throw "Expected x64 PE image: $Path"}
        $stream.Position=$offset+24;if($reader.ReadUInt16() -ne 0x20B){throw "Expected PE32+ image: $Path"}
    }finally{$stream.Dispose()}
}
$client=[IO.Path]::GetFullPath($ClientRoot).TrimEnd('\')
$stage=[IO.Path]::GetFullPath($StageRoot).TrimEnd('\')
if(!(Test-Path -LiteralPath $client -PathType Container)){throw 'Client folder does not exist'}
$manifest=Join-Path $stage 'ASEC-M3-SHA256.txt'
$entries=@();$seen=@{}
foreach($line in (Get-Content -LiteralPath $manifest)) {
    if($line -notmatch '^([A-Fa-f0-9]{64})  (.+)$'){throw 'Invalid M3 manifest line'}
    $expected=$Matches[1];$relative=$Matches[2].Replace('/','\')
    if($relative -ne 'WowClient64-ASEC.exe' -and $relative -ne 'XvidDecoder.dll' -and $relative -notmatch '^Interface\\AddOns\\WoW335ASEC\\[^\\]+\.(lua|toc)$' -and $relative -notmatch '^ASECM3\\Assets\\Textures\\[^\\]+\.dds$' -and $relative -notmatch '^ASECM3\\ProbeFixtures\\asec_event_activation_probe\.(m3|json)$'){throw "Unexpected manifest destination: $relative"}
    $target=[IO.Path]::GetFullPath((Join-Path $client $relative))
    $source=[IO.Path]::GetFullPath((Join-Path $stage $relative))
    if(!$target.StartsWith($client+'\',[StringComparison]::OrdinalIgnoreCase) -or !$source.StartsWith($stage+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Manifest path escaped its root'}
    if($seen.ContainsKey($target)){throw 'Duplicate manifest destination'};$seen[$target]=$true
    if((Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash -ine $expected){throw "Stage hash mismatch: $relative"}
    if($relative -eq 'WowClient64-ASEC.exe' -or $relative -eq 'XvidDecoder.dll'){Assert-X64Image $source}
    $entries+=@{relative=$relative;source=$source;target=$target;hash=$expected;existing=(Test-Path -LiteralPath $target)}
}
$exe=Join-Path $client 'WowClient64-ASEC.exe'
if(!$seen.ContainsKey($exe) -or !$seen.ContainsKey((Join-Path $client 'XvidDecoder.dll'))){throw 'Runtime manifest must include the client executable and XvidDecoder.dll'}
$running=@(foreach($candidate in(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($exe)) -ErrorAction SilentlyContinue)){
    if($candidate.HasExited){continue}
    $candidatePath=$candidate.Path
    if(!$candidatePath){throw 'Cannot inspect a matching client process; close it before installing'}
    if($candidatePath -ieq $exe){[pscustomobject]@{ProcessId=$candidate.Id;ExecutablePath=$candidatePath}}
})
if($running.Count -and !$Restart -and !$Probe){throw 'Canonical client is running; use -Restart to install and restart it'}
foreach($process in $running){Stop-Process -Id $process.ProcessId -Force;Wait-Process -Id $process.ProcessId -Timeout 15 -ErrorAction SilentlyContinue}
$backup=Join-Path $client ('ASECM3\Backups\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Force -Path $backup | Out-Null
$changed=@()
try {
    foreach($entry in $entries) {
        if($entry.existing -and (Get-FileHash -Algorithm SHA256 -LiteralPath $entry.target).Hash -ieq $entry.hash){continue}
        $saved=Join-Path $backup $entry.relative
        New-Item -ItemType Directory -Force -Path (Split-Path $saved) | Out-Null
        New-Item -ItemType Directory -Force -Path (Split-Path $entry.target) | Out-Null
        # Windows can retain an image lock after termination. Renaming the
        # canonical image on the same volume preserves it and avoids truncation.
        if($entry.existing){if($entry.relative -eq 'WowClient64-ASEC.exe'){Move-Item -LiteralPath $entry.target -Destination $saved}else{Copy-Item -LiteralPath $entry.target -Destination $saved}}
        $changed+=@{entry=$entry;saved=$saved}
        Copy-Item -LiteralPath $entry.source -Destination $entry.target
        if((Get-FileHash -Algorithm SHA256 -LiteralPath $entry.target).Hash -ine $entry.hash){throw "Installed hash mismatch: $($entry.relative)"}
    }
} catch {
    $failure=$_
    [array]::Reverse($changed)
    foreach($change in $changed) {
        $entry=$change.entry
        if(Test-Path -LiteralPath $entry.target){$failed=Join-Path $backup ('failed-new-files\'+$entry.relative);New-Item -ItemType Directory -Force -Path (Split-Path $failed)|Out-Null;Move-Item -LiteralPath $entry.target -Destination $failed}
        if($entry.existing -and (Test-Path -LiteralPath $change.saved)){Copy-Item -LiteralPath $change.saved -Destination $entry.target}
    }
    throw $failure
}
foreach($entry in $entries){if((Get-FileHash -Algorithm SHA256 -LiteralPath $entry.target).Hash -ine $entry.hash){throw "Final readback mismatch: $($entry.relative)"}}
Copy-Item -LiteralPath $manifest -Destination (Join-Path $backup 'installed-manifest.txt')
Write-Output "Verified $($entries.Count) installed files; backup: $backup"
if($Restart -or $Probe){$startOptions=@{FilePath=$exe;WorkingDirectory=$client;PassThru=$true};if($Probe){$startOptions.ArgumentList=@('-asecviewerprobe','-asecm3probe')};$started=Start-Process @startOptions;Write-Output "Canonical client PID $($started.Id)"}
