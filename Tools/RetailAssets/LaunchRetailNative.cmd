@echo off
setlocal
if not exist "%~dp0Data\Retail\RetailCasc.ini" (
  echo Install a native Retail content pack before launching.
  exit /b 1
)
if not exist "%~dp0WowClientRetailNative.exe" (
  echo Place WowClientRetailNative.exe alongside this launcher.
  exit /b 1
)
set "WOW_RETAIL_CONFIG=%~dp0Data\Retail\RetailCasc.ini"
set "WOW_RETAIL_PREVIEW="
set "WOW_RETAIL_EQUIP_PREVIEW="
set "WOW_RETAIL_MOUNT_PREVIEW="
set "WOW_RETAIL_WORLD_BIND_PREVIEW="
set "WOW_RETAIL_VERIFY_ITEM="
set "WOW_RETAIL_TRACE="
rem Native Retail models can exceed the D3D9 vertex constant limit.
start "" /D "%~dp0" "%~dp0WowClientRetailNative.exe" -d3d11
