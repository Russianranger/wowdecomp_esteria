param(
    [Parameter(Mandatory = $true)][string]$Config,
    [Parameter(Mandatory = $true)][string]$Downloader,
    [string]$LogDirectory
)
$ErrorActionPreference = 'Stop'
$configPath = (Resolve-Path -LiteralPath $Config).Path
$downloadPath = (Resolve-Path -LiteralPath $Downloader).Path
if (-not $LogDirectory) { $LogDirectory = Join-Path ([IO.Path]::GetDirectoryName($configPath)) 'Logs' }
[IO.Directory]::CreateDirectory($LogDirectory) | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-ffff'
$logPath = Join-Path $LogDirectory "forever-$stamp.log"
$errorPath = Join-Path $LogDirectory "forever-$stamp.err"
# The executable holds a source/config mutex, so repeated launches cannot duplicate this worker.
# This process never changes Retail's config, environment or existing downloader.
$worker = Start-Process -FilePath $downloadPath -ArgumentList @('forever', "`"$configPath`"", '95', '96') `
    -WorkingDirectory ([IO.Path]::GetDirectoryName($downloadPath)) -WindowStyle Hidden `
    -RedirectStandardOutput $logPath -RedirectStandardError $errorPath -PassThru
try { $worker.PriorityClass = 'BelowNormal' } catch { if (-not $worker.HasExited) { throw } }
Write-Output "Forever downloader started (process $($worker.Id)); progress: $logPath"
