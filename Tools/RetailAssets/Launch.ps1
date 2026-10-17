param(
    [Parameter(Mandatory = $true)][string]$ClientDirectory,
    [string]$Executable = 'WowClientRetailNative.exe',
    [uint32]$VerifyItem = 0
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
$start.EnvironmentVariables.Remove('WOW_RETAIL_PREVIEW')
$start.EnvironmentVariables.Remove('WOW_RETAIL_EQUIP_PREVIEW')
$start.EnvironmentVariables.Remove('WOW_RETAIL_MOUNT_PREVIEW')
$start.EnvironmentVariables.Remove('WOW_RETAIL_VERIFY_ITEM')
if ($VerifyItem) { $start.EnvironmentVariables['WOW_RETAIL_VERIFY_ITEM'] = "$VerifyItem" }
$client = [System.Diagnostics.Process]::Start($start)
Write-Output "Started native Retail packs client (process $($client.Id))."
