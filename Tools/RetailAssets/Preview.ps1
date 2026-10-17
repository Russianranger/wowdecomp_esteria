param(
    [Parameter(Mandatory = $true)][string]$ClientDirectory,
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$Config,
    [uint32]$ModelId = 916541,
    [uint32]$TextureId = 916543,
    [ValidateRange(0, 14)][uint32]$TextureType = 2,
    [switch]$Equipped
)

$ErrorActionPreference = 'Stop'
$clientRoot = (Resolve-Path -LiteralPath $ClientDirectory).Path
$configPath = (Resolve-Path -LiteralPath $Config).Path
$exePath = (Resolve-Path -LiteralPath (Join-Path $clientRoot $Executable)).Path
if ([IO.Path]::GetDirectoryName($exePath) -ne $clientRoot.TrimEnd('\', '/')) {
    throw 'Place the test executable alongside the client Data folder before launching.'
}
if (-not (Test-Path -LiteralPath (Join-Path $clientRoot 'Data') -PathType Container)) {
    throw 'The client directory must contain the 3.3.5a Data folder.'
}
if (-not $ModelId) { throw 'ModelId must be a positive FileDataID.' }

# Give only this process the selected config and preview request.
$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $exePath
$start.WorkingDirectory = $clientRoot
$start.UseShellExecute = $false
$start.EnvironmentVariables['WOW_RETAIL_CONFIG'] = $configPath
if ($Equipped) {
    if (-not $TextureId -or $TextureType -ne 2) { throw 'Equipped weapons require a texture ID in object-skin slot 2.' }
    $start.EnvironmentVariables.Remove('WOW_RETAIL_PREVIEW')
    $start.EnvironmentVariables['WOW_RETAIL_EQUIP_PREVIEW'] = "$ModelId $TextureId"
} else {
    $start.EnvironmentVariables.Remove('WOW_RETAIL_EQUIP_PREVIEW')
    $start.EnvironmentVariables['WOW_RETAIL_PREVIEW'] = "$ModelId $TextureId $TextureType"
}
$preview = [System.Diagnostics.Process]::Start($start)
Write-Output "Started Retail preview (process $($preview.Id))."
