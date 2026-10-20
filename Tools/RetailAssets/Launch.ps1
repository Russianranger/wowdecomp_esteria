param(
    [Parameter(Mandatory = $true)][string]$ClientDirectory,
    [string]$Executable = 'WowClientRetailNative.exe',
    [uint32]$VerifyItem = 0,
    [string]$ForeverConfig,
    [string]$Downloader = 'CdnAssetDownload.exe'
)
$ErrorActionPreference = 'Stop'
$clientRoot = (Resolve-Path -LiteralPath $ClientDirectory).Path
$configPath = (Resolve-Path -LiteralPath (Join-Path $clientRoot 'Data/Retail/RetailCasc.ini')).Path
$exePath = (Resolve-Path -LiteralPath (Join-Path $clientRoot $Executable)).Path
if ([IO.Path]::GetDirectoryName($exePath) -ne $clientRoot.TrimEnd('\', '/')) {
    throw 'The native client executable must be alongside the Data folder.'
}
$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $exePath
$start.WorkingDirectory = $clientRoot
$start.UseShellExecute = $false
$start.EnvironmentVariables['WOW_RETAIL_CONFIG'] = $configPath
if (-not $ForeverConfig) {
    $installedForever = Join-Path $clientRoot 'Data/Retail/ForeverCasc.ini'
    if (-not (Test-Path -LiteralPath $installedForever)) {
        $installedForever = Join-Path $clientRoot 'Data/Forever/ForeverCasc.ini'
    }
    if (Test-Path -LiteralPath $installedForever) { $ForeverConfig = $installedForever }
}
if ($ForeverConfig) {
    $foreverPath = (Resolve-Path -LiteralPath $ForeverConfig).Path
    $start.EnvironmentVariables['WOW_FOREVER_CONFIG'] = $foreverPath
    & (Join-Path $PSScriptRoot 'DownloadForever.ps1') -Config $foreverPath -Downloader (Join-Path $clientRoot $Downloader)
}
$start.EnvironmentVariables.Remove('WOW_RETAIL_PREVIEW')
$start.EnvironmentVariables.Remove('WOW_RETAIL_EQUIP_PREVIEW')
$start.EnvironmentVariables.Remove('WOW_RETAIL_MOUNT_PREVIEW')
$start.EnvironmentVariables.Remove('WOW_RETAIL_VERIFY_ITEM')
if ($VerifyItem) { $start.EnvironmentVariables['WOW_RETAIL_VERIFY_ITEM'] = "$VerifyItem" }
$client = [System.Diagnostics.Process]::Start($start)
Write-Output "Started native Retail packs client (process $($client.Id))."
