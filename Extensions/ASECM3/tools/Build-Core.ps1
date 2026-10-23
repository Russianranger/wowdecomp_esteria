param(
    [string]$KitRoot=$PSScriptRoot,
    [string]$OutputDirectory,
    [ValidateRange(1,64)][int]$Parallel=4,
    [string]$InspectModel,
    [string]$AnimationBank
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($KitRoot)
& (Join-Path $root 'Verify-Kit.ps1') -KitRoot $root
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'build/core'}
$output=[IO.Path]::GetFullPath($OutputDirectory)
$module=Join-Path $root 'Modules/ASECM3'
if($output -eq $module -or $output.StartsWith($module+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Build output must be outside the module sources'}
if($AnimationBank -and !$InspectModel){throw 'AnimationBank requires InspectModel'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if(!(Test-Path -LiteralPath $vswhere)){throw 'Visual Studio C++ tools and vswhere are required'}
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if($LASTEXITCODE -or !$installation){throw 'No Visual Studio x64 C++ toolchain found'}
& (Join-Path $installation 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64
$cmake=(Get-Command cmake -ErrorAction Stop).Source
Get-Command ninja -ErrorAction Stop | Out-Null
& $cmake -S $module -B $output -G Ninja -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE){throw 'Portable core CMake configuration failed'}
& $cmake --build $output --parallel $Parallel
if($LASTEXITCODE){throw 'Portable core build failed'}
$evidence=[ordered]@{builtAt=(Get-Date).ToString('o');kitManifestSHA256=(Get-FileHash -LiteralPath (Join-Path $root 'SHA256SUMS.txt')).Hash;architecture='x64';configuration='Release';coreSHA256=(Get-FileHash -LiteralPath (Join-Path $output 'ASECM3Core.lib')).Hash;scope='Standalone core build; no native host/runtime transfer proof';inspections=@()}
if($InspectModel){
    foreach($name in @('ASECM3AttachmentsInspect','ASECM3CollisionInspect','ASECM3PickingInspect')){
        $arguments=@($InspectModel);if($AnimationBank){$arguments+=$AnimationBank}
        $result=& (Join-Path $output ($name+'.exe')) @arguments
        if($LASTEXITCODE){throw "$name failed"}
        $data=$result|ConvertFrom-Json;if(!$data.ok){throw "$name did not report success"}
        $result|Set-Content -LiteralPath (Join-Path $output ($name+'.json'))
        $evidence.inspections+=$name
    }
}
$evidence|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $output 'build-evidence.json')
& (Join-Path $root 'Verify-Kit.ps1') -KitRoot $root
Write-Output "Portable core built at $output"
